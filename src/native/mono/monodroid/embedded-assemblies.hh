// Dear Emacs, this is a -*- C++ -*- header
#ifndef INC_MONODROID_EMBEDDED_ASSEMBLIES_H
#define INC_MONODROID_EMBEDDED_ASSEMBLIES_H

#include <array>
#include <cerrno>
#include <cstring>
#include <limits>
#include <string_view>
#include <tuple>
#include <vector>

#include <dirent.h>
#include <elf.h>
#include <semaphore.h>
#include <sys/mman.h>

#include <mono/metadata/object.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/mono-private-unstable.h>

#include "archive-dso-stub-config.hh"
#include "log_types.hh"
#include <runtime-base/strings.hh>
#include "xamarin-app.hh"
#include <shared/cpp-util.hh>
#include "cppcompat.hh"
#include "shared-constants.hh"
#include <shared/xxhash.hh>
#include "util.hh"

#include <concepts>

struct TypeMapHeader;

namespace xamarin::android::internal {
#if defined (DEBUG)
	struct TypeMappingInfo;
#endif

#if defined (RELEASE)
#define STATIC_IN_ANDROID_RELEASE static
#else
#define STATIC_IN_ANDROID_RELEASE
#endif // def RELEASE

	template<typename T>
	concept ByteArrayContainer = requires (T a) {
		a.size ();
		a.data ();
		requires std::same_as<typename T::value_type, uint8_t>;
	};

	template<typename T>
	concept LoaderData = requires (T a) {
		requires std::same_as<T, bool> || std::same_as<T, MonoAssemblyLoadContextGCHandle>;
	};

	class EmbeddedAssemblies final
	{
		struct md_mmap_info {
			void   *area;
			size_t  size;
		};

		struct ZipEntryLoadState
		{
			int                   file_fd;
			const char *          file_name;
			const char * const    prefix;
			uint32_t              prefix_len;
			size_t                buf_offset;
			uint16_t              compression_method;
			uint32_t              local_header_offset;
			uint32_t              data_offset;
			uint32_t              file_size;
			bool                  bundled_assemblies_slow_path;
			uint32_t              max_assembly_name_size;
			uint32_t              max_assembly_file_name_size;
		};

	private:
		static constexpr std::string_view ZIP_CENTRAL_MAGIC { "PK\1\2" };
		static constexpr std::string_view ZIP_LOCAL_MAGIC   { "PK\3\4" };
		static constexpr std::string_view ZIP_EOCD_MAGIC    { "PK\5\6" };
		static constexpr off_t ZIP_EOCD_LEN        = 22;
		static constexpr off_t ZIP_CENTRAL_LEN     = 46;
		static constexpr off_t ZIP_LOCAL_LEN       = 30;

		static constexpr std::string_view zip_path_separator { "/" };
		static constexpr std::string_view apk_lib_dir_name { "lib" };

		static constexpr size_t assemblies_prefix_size = calc_size(apk_lib_dir_name, zip_path_separator, SharedConstants::android_lib_abi, zip_path_separator);
		static constexpr auto assemblies_prefix_array = concat_string_views<assemblies_prefix_size> (apk_lib_dir_name, zip_path_separator, SharedConstants::android_lib_abi, zip_path_separator);

		// .data() must be used otherwise string_view length will include the trailing \0 in the array
		static constexpr std::string_view assemblies_prefix { assemblies_prefix_array.data () };

		// We have two records for each assembly, for names with and without the extension
		static constexpr uint32_t assembly_store_index_entries_per_assembly = 2;
		static constexpr uint32_t number_of_assembly_store_files = 1;
		static constexpr std::string_view dso_suffix { ".so" };

		static constexpr std::string_view apk_lib_prefix = assemblies_prefix; // concat_const (apk_lib_dir_name, zip_path_separator, SharedConstants::android_lib_abi, zip_path_separator);
		static constexpr std::string_view assembly_store_prefix { "libassemblies." };
		static constexpr std::string_view assembly_store_extension { ".blob" };

		static constexpr size_t assembly_store_file_name_size = calc_size (assembly_store_prefix, SharedConstants::android_lib_abi, assembly_store_extension, dso_suffix);
		static constexpr auto assembly_store_file_name_array = concat_string_views<assembly_store_file_name_size> (assembly_store_prefix, SharedConstants::android_lib_abi, assembly_store_extension, dso_suffix);

		// .data() must be used otherwise string_view length will include the trailing \0 in the array
		static constexpr std::string_view assembly_store_file_name { assembly_store_file_name_array.data () };

		static constexpr size_t assembly_store_file_path_size = calc_size(apk_lib_dir_name, zip_path_separator, SharedConstants::android_lib_abi, zip_path_separator, assembly_store_prefix, SharedConstants::android_lib_abi, assembly_store_extension, dso_suffix);
		static constexpr auto assembly_store_file_path_array = concat_string_views<assembly_store_file_path_size> (apk_lib_dir_name, zip_path_separator, SharedConstants::android_lib_abi, zip_path_separator, assembly_store_prefix, SharedConstants::android_lib_abi, assembly_store_extension, dso_suffix);
		// .data() must be used otherwise string_view length will include the trailing \0 in the array
		static constexpr std::string_view assembly_store_file_path { assembly_store_file_path_array.data () };

		static constexpr size_t dso_size_overhead = ArchiveDSOStubConfig::PayloadSectionOffset + (ArchiveDSOStubConfig::SectionHeaderEntryCount * ArchiveDSOStubConfig::SectionHeaderEntrySize);

	public:
		/* filename is e.g. System.dll, System.dll.mdb, System.pdb */
		using monodroid_should_register = bool (*)(const char *filename);

	public:
#if defined (RELEASE)
		EmbeddedAssemblies () noexcept
		{}
#endif  // def RELEASE

		static const char* typemap_managed_to_java (MonoReflectionType *type, const uint8_t *mvid) noexcept;

		static void install_preload_hooks_for_appdomains () noexcept;
		static void install_preload_hooks_for_alc () noexcept;
		static MonoReflectionType* typemap_java_to_managed (MonoString *java_type) noexcept;

		/* returns current number of *all* assemblies found from all invocations */
		template<bool (*should_register_fn)(const char*)>
		static size_t register_from_apk (const char *apk_file) noexcept
		{
			static_assert (should_register_fn != nullptr, "should_register_fn is a required template parameter");
			return register_from_apk (apk_file, should_register_fn);
		}

		template<bool (*should_register_fn)(const char*)>
		static size_t register_from_filesystem () noexcept
		{
			static_assert (should_register_fn != nullptr, "should_register_fn is a required template parameter");
			return register_from_filesystem (should_register_fn);
		}

		static constexpr decltype(assemblies_prefix) const& get_assemblies_prefix () noexcept
		{
			return assemblies_prefix;
		}

		static bool get_register_debug_symbols () noexcept
		{
			return register_debug_symbols;
		}

		static void set_register_debug_symbols (bool value) noexcept
		{
			register_debug_symbols = value;
		}

		static void set_assemblies_prefix (const char *prefix) noexcept;

		static void get_runtime_config_blob (const char *& area, uint32_t& size) noexcept
		{
			area = static_cast<char*>(runtime_config_data);

			abort_unless (
				runtime_config_data_size < std::numeric_limits<uint32_t>::max (),
				[] {
					return detail::_format_message ("Runtime config binary blob size exceeds %u bytes",
													std::numeric_limits<uint32_t>::max ());
				}
			);
			size = static_cast<uint32_t>(runtime_config_data_size);
		}

		static void unmap_runtime_config_blob () noexcept
		{
			if (runtime_config_blob_mmap.area == nullptr) {
				return;
			}

			munmap (runtime_config_blob_mmap.area, runtime_config_blob_mmap.size);
			runtime_config_blob_mmap.area = nullptr;
			runtime_config_blob_mmap.size = 0uz;
			runtime_config_data = nullptr;
			runtime_config_data_size = 0uz;
		}

		static bool have_runtime_config_blob () noexcept
		{
			return application_config.have_runtime_config_blob && runtime_config_blob_mmap.area != nullptr;
		}

		static bool keep_scanning () noexcept
		{
			return need_to_scan_more_apks;
		}

		static void ensure_valid_assembly_stores () noexcept
		{
			if (!application_config.have_assembly_store) {
				return;
			}

			abort_unless (assembly_store_hashes != nullptr, "Invalid or incomplete assembly store data");
		}

	private:
		static const char* typemap_managed_to_java (MonoType *type, MonoClass *klass, const uint8_t *mvid) noexcept;
		static MonoReflectionType* typemap_java_to_managed (hash_t hash, const MonoString *java_type_name) noexcept;
		static size_t register_from_apk (const char *apk_file, monodroid_should_register should_register) noexcept;
		static size_t register_from_filesystem (monodroid_should_register should_register) noexcept;
		static size_t register_from_filesystem (const char *dir, bool look_for_mangled_names, monodroid_should_register should_register) noexcept;

		template<bool MangledNamesMode>
		static bool maybe_register_assembly_from_filesystem (monodroid_should_register should_register, size_t& assembly_count, const dirent* dir_entry, ZipEntryLoadState& state) noexcept;
		static bool maybe_register_blob_from_filesystem (monodroid_should_register should_register, size_t& assembly_count, const dirent* dir_entry, ZipEntryLoadState& state) noexcept;

		static void gather_bundled_assemblies_from_apk (const char* apk, monodroid_should_register should_register) noexcept;

		template<LoaderData TLoaderData>
		static MonoAssembly* individual_assemblies_open_from_bundles (dynamic_local_string<SENSIBLE_PATH_MAX>& name, TLoaderData loader_data, bool ref_only) noexcept;

		template<LoaderData TLoaderData>
		static MonoAssembly* assembly_store_open_from_bundles (dynamic_local_string<SENSIBLE_PATH_MAX>& name, TLoaderData loader_data, bool ref_only) noexcept;

		template<LoaderData TLoaderData>
		static MonoAssembly* open_from_bundles (MonoAssemblyName* aname, TLoaderData loader_data, MonoError *error, bool ref_only) noexcept;

		template<bool LogMapping>
		static void map_runtime_file (XamarinAndroidBundledAssembly& file) noexcept;
		static void map_assembly (XamarinAndroidBundledAssembly& file) noexcept;
		static void map_debug_data (XamarinAndroidBundledAssembly& file) noexcept;

		template<LoaderData TLoaderData>
		static MonoAssembly* load_bundled_assembly (
			XamarinAndroidBundledAssembly& assembly,
			dynamic_local_string<SENSIBLE_PATH_MAX> const& name,
			dynamic_local_string<SENSIBLE_PATH_MAX> const& abi_name,
			TLoaderData loader_data,
			bool ref_only) noexcept;

#if defined (DEBUG)
		template<typename H>
		bool typemap_read_header (int dir_fd, const char *file_type, const char *dir_path, const char *file_path, uint32_t expected_magic, H &header, size_t &file_size, int &fd);
		std::unique_ptr<uint8_t[]> typemap_load_index (int dir_fd, const char *dir_path, const char *index_path);
		std::unique_ptr<uint8_t[]> typemap_load_index (TypeMapIndexHeader &header, size_t file_size, int index_fd);
		bool typemap_load_file (int dir_fd, const char *dir_path, const char *file_path, TypeMap &module);
		bool typemap_load_file (BinaryTypeMapHeader &header, const char *dir_path, const char *file_path, int file_fd, TypeMap &module);
		static ssize_t do_read (int fd, void *buf, size_t count);
		static const TypeMapEntry *typemap_managed_to_java (const char *managed_type_name) noexcept;
#endif // DEBUG

		static md_mmap_info md_mmap_apk_file (int fd, uint32_t offset, size_t size, const char* filename);
		static MonoAssembly* open_from_bundles_full (MonoAssemblyName *aname, char **assemblies_path, void *user_data);
		static MonoAssembly* open_from_bundles (MonoAssemblyLoadContextGCHandle alc_gchandle, MonoAssemblyName *aname, char **assemblies_path, void *user_data, MonoError *error);

		static void set_assembly_data_and_size (uint8_t* source_assembly_data, uint32_t source_assembly_data_size, uint8_t*& dest_assembly_data, uint32_t& dest_assembly_data_size) noexcept;
		static void get_assembly_data (uint8_t *data, uint32_t data_size, const char *name, uint8_t*& assembly_data, uint32_t& assembly_data_size) noexcept;
		static void get_assembly_data (XamarinAndroidBundledAssembly const& e, uint8_t*& assembly_data, uint32_t& assembly_data_size) noexcept;
		static void get_assembly_data (AssemblyStoreSingleAssemblyRuntimeData const& e, uint8_t*& assembly_data, uint32_t& assembly_data_size) noexcept;

		static void zip_load_entries (int fd, const char *apk_name, monodroid_should_register should_register) noexcept;
		static void zip_load_individual_assembly_entries (std::vector<uint8_t> const& buf, uint32_t num_entries, monodroid_should_register should_register, ZipEntryLoadState &state) noexcept;
		static void zip_load_assembly_store_entries (std::vector<uint8_t> const& buf, uint32_t num_entries, ZipEntryLoadState &state) noexcept;
		static bool zip_load_entry_common (size_t entry_index, std::vector<uint8_t> const& buf, dynamic_local_string<SENSIBLE_PATH_MAX> &entry_name, ZipEntryLoadState &state) noexcept;
		static bool zip_read_cd_info (int fd, uint32_t& cd_offset, uint32_t& cd_size, uint16_t& cd_entries) noexcept;
		static bool zip_adjust_data_offset (int fd, ZipEntryLoadState &state) noexcept;

		template<size_t BufSize>
		static bool zip_extract_cd_info (std::array<uint8_t, BufSize> const& buf, uint32_t& cd_offset, uint32_t& cd_size, uint16_t& cd_entries) noexcept;

		template<class T>
		static bool zip_ensure_valid_params (T const& buf, size_t index, size_t to_read) noexcept;

		template<ByteArrayContainer T>
		static bool zip_read_field (T const& src, size_t source_index, uint16_t& dst) noexcept;

		template<ByteArrayContainer T>
		static bool zip_read_field (T const& src, size_t source_index, uint32_t& dst) noexcept;

		template<ByteArrayContainer T>
		static bool zip_read_field (T const& src, size_t source_index, std::array<uint8_t, 4>& dst_sig) noexcept;

		template<ByteArrayContainer T>
		static bool zip_read_field (T const& buf, size_t index, size_t count, dynamic_local_string<SENSIBLE_PATH_MAX>& characters) noexcept;

		static bool zip_read_entry_info (std::vector<uint8_t> const& buf, dynamic_local_string<SENSIBLE_PATH_MAX>& file_name, ZipEntryLoadState &state) noexcept;

		[[gnu::always_inline]]
		static std::tuple<void*, size_t> get_wrapper_dso_payload_pointer_and_size (md_mmap_info const& map_info, const  char *file_name) noexcept
		{
			using Elf_Header = std::conditional_t<SharedConstants::is_64_bit_target, Elf64_Ehdr, Elf32_Ehdr>;
			using Elf_SHeader = std::conditional_t<SharedConstants::is_64_bit_target, Elf64_Shdr, Elf32_Shdr>;

			const void* const mapped_elf = map_info.area;
			auto elf_bytes = static_cast<const uint8_t* const>(mapped_elf);
			auto elf_header = reinterpret_cast<const Elf_Header*const>(mapped_elf);

			if constexpr (SharedConstants::debug_build) {
				// In debug mode we might be dealing with plain data, without DSO wrapper
				if (elf_header->e_ident[EI_MAG0] != ELFMAG0 ||
						elf_header->e_ident[EI_MAG1] != ELFMAG1 ||
						elf_header->e_ident[EI_MAG2] != ELFMAG2 ||
						elf_header->e_ident[EI_MAG3] != ELFMAG3) {
					log_debug (LOG_ASSEMBLY, "Not an ELF image: {}", optional_string (file_name));
					// Not an ELF image, just return what we mmapped before
					return { map_info.area, map_info.size };
				}
			}

			auto section_header = reinterpret_cast<const Elf_SHeader*const>(elf_bytes + elf_header->e_shoff);
			Elf_SHeader const& payload_hdr = section_header[ArchiveDSOStubConfig::PayloadSectionIndex];

			return {
				const_cast<void*>(reinterpret_cast<const void*const> (elf_bytes + ArchiveDSOStubConfig::PayloadSectionOffset)),
				payload_hdr.sh_size
			};
		}

		[[gnu::always_inline]]
		static void store_mapped_runtime_config_data (md_mmap_info const& map_info, const char *file_name) noexcept
		{
			auto [payload_start, payload_size] = get_wrapper_dso_payload_pointer_and_size (map_info, file_name);
			log_debug (LOG_ASSEMBLY, "Runtime config: payload pointer {:p} ; size {}", payload_start, payload_size);
			runtime_config_data = payload_start;
			runtime_config_data_size = payload_size;
			runtime_config_blob_found = true;
		}

		static std::tuple<const char*, uint32_t> get_assemblies_prefix_and_length () noexcept
		{
			if (assemblies_prefix_override != nullptr) {
				return { assemblies_prefix_override, static_cast<uint32_t>(strlen (assemblies_prefix_override)) };
			}

			if (application_config.have_assembly_store) {
				return { apk_lib_prefix.data (), apk_lib_prefix.size () };
			}

			return {assemblies_prefix.data (), assemblies_prefix.size () };
		}

		static bool all_required_zip_entries_found () noexcept
		{
			return
				number_of_mapped_assembly_stores == number_of_assembly_store_files && number_of_zip_dso_entries >= application_config.number_of_shared_libraries
				&& ((application_config.have_runtime_config_blob && runtime_config_blob_found) || !application_config.have_runtime_config_blob);
		}

		[[gnu::always_inline]] static c_unique_ptr<char> to_utf8 (const MonoString *s) noexcept
		{
			if (s == nullptr) [[unlikely]] {
				// We need to duplicate mono_string_to_utf8 behavior
				return c_unique_ptr<char> (strdup ("<null>"));
			}

			return c_unique_ptr<char> (mono_string_to_utf8 (const_cast<MonoString*>(s)));
		}

		template<typename Key, typename Entry, int (*compare)(const Key*, const Entry*), bool use_extra_size = false>
		static const Entry* binary_search (const Key *key, const Entry *base, size_t nmemb, size_t extra_size = 0uz) noexcept;

#if defined (DEBUG)
		static int compare_type_name (const char *type_name, const TypeMapEntry *entry) noexcept;
#else
		static int compare_mvid (const uint8_t *mvid, const TypeMapModule *module) noexcept;
		static const TypeMapModuleEntry* binary_search (uint32_t key, const TypeMapModuleEntry *arr, uint32_t n) noexcept;
#endif
		template<bool NeedsNameAlloc>
		static void set_entry_data (XamarinAndroidBundledAssembly &entry, ZipEntryLoadState const& state, dynamic_local_string<SENSIBLE_PATH_MAX> const& entry_name) noexcept;
		static void set_assembly_entry_data (XamarinAndroidBundledAssembly &entry, ZipEntryLoadState const& state, dynamic_local_string<SENSIBLE_PATH_MAX> const& entry_name) noexcept;
		static void set_debug_entry_data (XamarinAndroidBundledAssembly &entry, ZipEntryLoadState const& state, dynamic_local_string<SENSIBLE_PATH_MAX> const& entry_name) noexcept;
		static void map_assembly_store (dynamic_local_string<SENSIBLE_PATH_MAX> const& entry_name, ZipEntryLoadState &state) noexcept;
		static const AssemblyStoreIndexEntry* find_assembly_store_entry (hash_t hash, const AssemblyStoreIndexEntry *entries, size_t entry_count) noexcept;
		static void store_individual_assembly_data (dynamic_local_string<SENSIBLE_PATH_MAX> const& entry_name, ZipEntryLoadState const& state, monodroid_should_register should_register) noexcept;

		constexpr static size_t get_mangled_name_max_size_overhead ()
		{
			return SharedConstants::MANGLED_ASSEMBLY_NAME_EXT.size() +
				   std::max (SharedConstants::MANGLED_ASSEMBLY_REGULAR_ASSEMBLY_MARKER.size(), SharedConstants::MANGLED_ASSEMBLY_SATELLITE_ASSEMBLY_MARKER.size()) +
				   1; // For the extra `-` char in the culture portion of satellite assembly's name
		}

		static void configure_state_for_individual_assembly_load (ZipEntryLoadState& state) noexcept
		{
			state.bundled_assemblies_slow_path = bundled_assembly_index >= application_config.number_of_assemblies_in_apk;
			state.max_assembly_name_size = application_config.bundled_assembly_name_width - 1;

			// Enough room for the mangle character at the start, plus the extra extension
			state.max_assembly_file_name_size = static_cast<uint32_t>(state.max_assembly_name_size + get_mangled_name_max_size_overhead ());
		}

		template<bool IsSatelliteAssembly>
		static constexpr size_t get_mangled_prefix_length ()
		{
			if constexpr (IsSatelliteAssembly) {
				return SharedConstants::MANGLED_ASSEMBLY_SATELLITE_ASSEMBLY_MARKER.length ();
			} else {
				return SharedConstants::MANGLED_ASSEMBLY_REGULAR_ASSEMBLY_MARKER.length ();
			}
		}

		template<bool IsSatelliteAssembly>
		static constexpr size_t get_mangled_data_size ()
		{
			return SharedConstants::MANGLED_ASSEMBLY_NAME_EXT.length () + get_mangled_prefix_length<IsSatelliteAssembly> ();
		}

		template<bool IsSatelliteAssembly>
		static void unmangle_name (dynamic_local_string<SENSIBLE_PATH_MAX> &name, size_t start_idx = 0uz) noexcept
		{
			constexpr size_t mangled_data_size = get_mangled_data_size<IsSatelliteAssembly> ();
			if (name.length () <= mangled_data_size) {
				// Nothing to do, the name is too short
				return;
			}

			size_t new_size = name.length () - mangled_data_size;
			memmove (name.get () + start_idx, name.get () + start_idx + get_mangled_prefix_length<IsSatelliteAssembly> (), new_size);
			name.set_length (new_size);

			if constexpr (IsSatelliteAssembly) {
				// Make sure assembly name is {CULTURE}/assembly.dll
				for (size_t idx = start_idx; idx < name.length (); idx++) {
					if (name[idx] == SharedConstants::SATELLITE_CULTURE_END_MARKER_CHAR) {
						name[idx] = '/';
						break;
					}
				}
			}
			log_debug (LOG_ASSEMBLY, "Unmangled name to '{}'", optional_string (name.get ()));
		};

	private:
		static inline constexpr bool UnmangleSatelliteAssembly = true;
		static inline constexpr bool UnmangleRegularAssembly = false;

		static inline std::vector<XamarinAndroidBundledAssembly> *bundled_debug_data = nullptr;
		static inline std::vector<XamarinAndroidBundledAssembly> *extra_bundled_assemblies = nullptr;

		static inline bool     register_debug_symbols;
		static inline bool     have_and_want_debug_symbols;
		static inline size_t   bundled_assembly_index = 0uz;
		static inline size_t   number_of_found_assemblies = 0uz;

#if defined (DEBUG)
		TypeMappingInfo       *java_to_managed_maps;
		TypeMappingInfo       *managed_to_java_maps;
		TypeMap               *type_maps;
		size_t                 type_map_count;
#endif // DEBUG
		static inline const char   *assemblies_prefix_override = nullptr;

		static inline md_mmap_info  runtime_config_blob_mmap{};
		static inline void         *runtime_config_data = nullptr;
		static inline size_t        runtime_config_data_size = 0uz;
		static inline bool          runtime_config_blob_found = false;
		static inline uint32_t      number_of_mapped_assembly_stores = 0u;
		static inline uint32_t      number_of_zip_dso_entries = 0u;
		static inline bool          need_to_scan_more_apks = true;

		static inline AssemblyStoreIndexEntry *assembly_store_hashes = nullptr;
		static inline xamarin::android::mutex  assembly_decompress_mutex {};
	};
}

#endif /* INC_MONODROID_EMBEDDED_ASSEMBLIES_H */
