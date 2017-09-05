﻿using System;
using System.CodeDom;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Xml;

namespace Xamarin.Android.Tasks
{
	class ManagedResourceParser : ResourceParser
	{
		CodeTypeDeclaration resources;
		CodeTypeDeclaration layout, ids, drawable, strings, colors, dimension, raw, animation, attrib, boolean, ints, styleable, style, arrays;
		Dictionary<string, string> map;

		void SortMembers (CodeTypeDeclaration decl)
		{
			CodeTypeMember [] members = new CodeTypeMember [decl.Members.Count];
			decl.Members.CopyTo (members, 0);
			decl.Members.Clear ();
			Array.Sort (members, (x, y) => string.Compare (x.Name, y.Name, StringComparison.OrdinalIgnoreCase));
			decl.Members.AddRange (members);
		}

		public CodeTypeDeclaration Parse (string resourceDirectory, IEnumerable<string> additionalResourceDirectories, bool isApp, Dictionary<string, string> resourceMap)
		{
			if (!Directory.Exists (resourceDirectory))
				throw new ArgumentException ("Specified resource directory was not found: " + resourceDirectory);

			map = resourceMap ?? new Dictionary<string, string> ();
			resources = CreateResourceClass ();
			animation = CreateClass ("Animation");
			arrays = CreateClass ("Array");
			attrib = CreateClass ("Attribute");
			boolean = CreateClass ("Boolean");
			layout = CreateClass ("Layout");
			ids = CreateClass ("Id");
			ints = CreateClass ("Integer");
			drawable = CreateClass ("Drawable");
			strings = CreateClass ("String");
			colors = CreateClass ("Color");
			dimension = CreateClass ("Dimension");
			raw = CreateClass ("Raw");
			styleable = CreateClass ("Styleable");
			style = CreateClass ("Style");

			foreach (var dir in Directory.GetDirectories (resourceDirectory, "*", SearchOption.TopDirectoryOnly)) {
				foreach (var file in Directory.GetFiles (dir, "*.*", SearchOption.AllDirectories)) {
					ProcessResourceFile (file);
				}
			}
			if (additionalResourceDirectories != null) {
				foreach (var dir in additionalResourceDirectories) {
					foreach (var file in Directory.GetFiles (dir, "*.*", SearchOption.AllDirectories)) {
						ProcessResourceFile (file);
					}
				}
			}

			SortMembers (animation);
			SortMembers (ids);
			SortMembers (attrib);
			SortMembers (arrays);
			SortMembers (boolean);
			SortMembers (colors);
			SortMembers (dimension);
			SortMembers (drawable);
			SortMembers (ints);
			SortMembers (layout);
			SortMembers (raw);
			SortMembers (strings);
			SortMembers (style);
			SortMembers (styleable);


			if (animation.Members.Count > 1)
				resources.Members.Add (animation);
			if (arrays.Members.Count > 1)
				resources.Members.Add (arrays);
			if (attrib.Members.Count > 1)
				resources.Members.Add (attrib);
			if (boolean.Members.Count > 1)
				resources.Members.Add (boolean);
			if (colors.Members.Count > 1)
				resources.Members.Add (colors);
			if (dimension.Members.Count > 1)
				resources.Members.Add (dimension);
			if (drawable.Members.Count > 1)
				resources.Members.Add (drawable);
			if (ids.Members.Count > 1)
				resources.Members.Add (ids);
			if (ints.Members.Count > 1)
				resources.Members.Add (ints);
			if (layout.Members.Count > 1)
				resources.Members.Add (layout);
			if (raw.Members.Count > 1)
				resources.Members.Add (raw);
			if (strings.Members.Count > 1)
				resources.Members.Add (strings);
			if (style.Members.Count > 1)
				resources.Members.Add (style);
			if (styleable.Members.Count > 1)
				resources.Members.Add (styleable);

			return resources;
		}

		void ProcessResourceFile (string file)
		{
			var fileName = Path.GetFileNameWithoutExtension (file);
			if (string.IsNullOrEmpty (fileName))
				return;
			if (fileName.EndsWith (".9", StringComparison.OrdinalIgnoreCase))
				fileName = Path.GetFileNameWithoutExtension (fileName);
			var path = Directory.GetParent (file).Name;
			var ext = Path.GetExtension (file);
			switch (ext) {
			case ".xml":
			case ".axml":
				if (string.Compare (path, "raw", StringComparison.OrdinalIgnoreCase) == 0)
					goto default;
				ProcessXmlFile (file);
				break;
			default:
				break;
			}
			CreateResourceField (path, fileName);
		}

		CodeTypeDeclaration CreateResourceClass ()
		{
			var decl = new CodeTypeDeclaration ("Resource") {
				IsPartial = true,
			};
			var asm = Assembly.GetExecutingAssembly ().GetName ();
			var codeAttrDecl =
				new CodeAttributeDeclaration ("System.CodeDom.Compiler.GeneratedCodeAttribute",
					new CodeAttributeArgument (
						new CodePrimitiveExpression (asm.Name)),
					new CodeAttributeArgument (
						new CodePrimitiveExpression (asm.Version.ToString ()))
				);
			decl.CustomAttributes.Add (codeAttrDecl);
			return decl;
		}

		CodeTypeDeclaration CreateClass (string type)
		{
			var t = new CodeTypeDeclaration (ResourceParser.GetNestedTypeName (type)) {
				IsPartial = true,
				TypeAttributes = TypeAttributes.Public,
			};
			t.Members.Add (new CodeConstructor () {
				Attributes = MemberAttributes.Private,
			});
			return t;
		}

		void CreateField (CodeTypeDeclaration parentType, string name, Type type)
		{
			var f = new CodeMemberField (type, name) {
				// pity I can't make the member readonly...
				Attributes = MemberAttributes.Public | MemberAttributes.Static,
			};
			parentType.Members.Add (f);
		}

		void CreateIntField (CodeTypeDeclaration parentType, string name)
		{
			string mappedName = GetResourceName (parentType.Name, name, map);
			if (parentType.Members.OfType<CodeTypeMember> ().Any (x => string.Compare (x.Name, mappedName, StringComparison.OrdinalIgnoreCase) == 0))
				return;
			var f = new CodeMemberField (typeof (int), mappedName) {
				// pity I can't make the member readonly...
				Attributes = MemberAttributes.Static | MemberAttributes.Public,
				InitExpression = new CodePrimitiveExpression (0),
				Comments = {
						new CodeCommentStatement ("aapt resource value: 0"),
					},
			};
			parentType.Members.Add (f);
		}

		void CreateIntArrayField (CodeTypeDeclaration parentType, string name, int count)
		{
			string mappedName = GetResourceName (parentType.Name, name, map);
			if (parentType.Members.OfType<CodeTypeMember> ().Any (x => string.Compare (x.Name, mappedName, StringComparison.OrdinalIgnoreCase) == 0))
				return;
			var f = new CodeMemberField (typeof (int []), name) {
				// pity I can't make the member readonly...
				Attributes = MemberAttributes.Public | MemberAttributes.Static,
			};
			CodeArrayCreateExpression c = (CodeArrayCreateExpression)f.InitExpression;
			if (c == null) {
				f.InitExpression = c = new CodeArrayCreateExpression (typeof (int []));
			}
			for (int i = 0; i < count; i++)
				c.Initializers.Add (new CodePrimitiveExpression (0));

			parentType.Members.Add (f);
		}

		HashSet<string> resourceNamesToUseDirectly = new HashSet<string> () {
				"integer-array",
				"string-array",
				"declare-styleable",
				"add-resource",
			};

		void CreateResourceField (string root, string fieldName, XmlReader element = null)
		{
			var i = root.IndexOf ('-');
			var item = i < 0 ? root : root.Substring (0, i);
			item = resourceNamesToUseDirectly.Contains (root) ? root : item;
			switch (item.ToLower ()) {
			case "bool":
				CreateIntField (boolean, fieldName);
				break;
			case "color":
				CreateIntField (colors, fieldName);
				break;
			case "drawable":
				CreateIntField (drawable, fieldName);
				break;
			case "dimen":
			case "fraction":
				CreateIntField (dimension, fieldName);
				break;
			case "integer":
				CreateIntField (ints, fieldName);
				break;
			case "anim":
				CreateIntField (animation, fieldName);
				break;
			case "attr":
				CreateIntField (attrib, fieldName);
				break;
			case "layout":
				CreateIntField (layout, fieldName);
				break;
			case "raw":
				CreateIntField (raw, fieldName);
				break;
			case "string":
				CreateIntField (strings, fieldName);
				break;
			case "enum":
			case "flag":
			case "id":
				CreateIntField (ids, fieldName);
				break;
			case "integer-array":
			case "string-array":
				CreateIntField (arrays, fieldName);
				break;
			case "configVarying":
			case "add-resource":
			case "declare-styleable":
				ProcessStyleable (element);
				break;
			case "style":
				CreateIntField (style, fieldName.Replace (".", "_"));
				break;
			default:
				break;
			}
		}

		void ProcessStyleable (XmlReader reader)
		{
			string topName = null;
			int fieldCount = 0;
			while (reader.Read ()) {
				if (reader.NodeType == XmlNodeType.Whitespace || reader.NodeType == XmlNodeType.Comment)
					continue;
				string name = null;
				if (string.IsNullOrEmpty (topName)) {
					if (reader.HasAttributes) {
						while (reader.MoveToNextAttribute ()) {
							if (reader.Name.Replace ("android:", "") == "name")
								topName = reader.Value;
						}
					}
				}
				if (!reader.IsStartElement () || reader.LocalName == "declare-styleable")
					continue;
				if (reader.HasAttributes) {
					while (reader.MoveToNextAttribute ()) {
						if (reader.Name.Replace ("android:", "") == "name")
							name = reader.Value;
					}
				}
				reader.MoveToElement ();
				if (reader.LocalName == "attr") {
					CreateIntField (styleable, $"{topName}_{name.Replace (":", "_")}");
					if (!name.StartsWith ("android:", StringComparison.OrdinalIgnoreCase))
						CreateIntField (attrib, name);
					fieldCount++;
				} else {
					if (name != null)
						CreateIntField (ids, $"{name.Replace (":", "_")}");
				}
			}
			CreateIntArrayField (styleable, topName, fieldCount);
		}

		void ProcessXmlFile (string file)
		{
			using (var reader = XmlReader.Create (file)) {
				while (reader.Read ()) {
					if (reader.NodeType == XmlNodeType.Whitespace || reader.NodeType == XmlNodeType.Comment)
						continue;
					if (reader.IsStartElement ()) {
						var elementName = reader.Name;
						if (reader.HasAttributes) {
							string name = null;
							string type = null;
							string id = null;
							while (reader.MoveToNextAttribute ()) {
								if (reader.LocalName == "name")
									name = reader.Value;
								if (reader.LocalName == "type")
									type = reader.Value;
								if (reader.LocalName == "id")
									id = reader.Value.Replace ("@+id/", "").Replace ("@id/", ""); ;
							}
							if (name?.Contains ("android:") ?? false)
								continue;
							if (id?.Contains ("android:") ?? false)
								continue;
							// Move the reader back to the element node.
							reader.MoveToElement ();
							if (!string.IsNullOrEmpty (name))
								CreateResourceField (type ?? elementName, name, reader.ReadSubtree ());
							if (!string.IsNullOrEmpty (id)) {
								CreateIntField (ids, id);
							}
						}
					}
				}
			}
		}
	}
}
