using NUnit.Framework;
using System.IO;
using System.Text;
using Xamarin.ProjectTools;

namespace Xamarin.Android.Build.Tests
{
	[Category ("Node-3")]
	[Parallelizable (ParallelScope.Children)]
	public class DynamicFeatureTests : BaseTest
	{
		[Test]
		[Category ("SmokeTests")]
		public void BuildDynamicAssetFeature ([Values (true, false)] bool isRelease) {

			if (!Builder.UseDotNet)
				Assert.Ignore ("Dynamic Features not supported on Legacy Projects.");
			var path = Path.Combine ("temp", TestName);
			var feature1 = new XamarinAndroidLibraryProject () {
				ProjectName = "Feature1",
				IsRelease = isRelease,
				OtherBuildItems = {
					new AndroidItem.AndroidAsset ("Assets\\asset3.txt") {
						TextContent = () => "Asset3",
						Encoding = Encoding.ASCII,
					},
				}
			};
			// we don't need any of this stuff!
			feature1.Sources.Clear ();
			feature1.AndroidResources.Clear ();
			feature1.SetProperty ("FeatureType", "AssetPack");
			var app = new XamarinAndroidApplicationProject {
				ProjectName = "MyApp",
				IsRelease = isRelease,
			};
			app.SetProperty ("AndroidPackageFormat", "aab");
			var reference = new BuildItem ("ProjectReference", $"..\\{feature1.ProjectName}\\{feature1.ProjectName}.csproj");
			app.References.Add (reference);
			using (var libBuilder = CreateDllBuilder (Path.Combine (path, feature1.ProjectName))) {
				libBuilder.Save (feature1);
				using (var appBuilder = CreateApkBuilder (Path.Combine (path, app.ProjectName))) {
					Assert.IsTrue (appBuilder.Build (app), $"{app.ProjectName} should succeed");
					// Check the final aab has the required feature files in it.
					var aab = Path.Combine (Root, appBuilder.ProjectDirectory,
						app.OutputPath, $"{app.PackageName}.aab");
					using (var zip = ZipHelper.OpenZip (aab)) {
						Assert.IsTrue (zip.ContainsEntry ("feature1/assets/asset3.txt"), "aab should contain feature1/assets/asset3.txt");
						Assert.IsTrue (zip.ContainsEntry ("feature1/assets.pb"), "aab should contain feature1/assets.pb");
						Assert.IsFalse (zip.ContainsEntry ("feature1/resources.pb"), "aab should not contain feature1/resources.pb");
					}
				}
			}
		}

		[Test]
		[Category ("SmokeTests")]
		public void BuildDynamicActivityFeature ([Values (true, false)] bool isRelease) {

			if (!Builder.UseDotNet)
				Assert.Ignore ("Dynamic Features not supported on Legacy Projects.");
			var path = Path.Combine ("temp", TestName);
			var assetFeature = new XamarinAndroidLibraryProject () {
				ProjectName = "AssetFeature",
				IsRelease = isRelease,
				OtherBuildItems = {
					new AndroidItem.AndroidAsset ("Assets\\asset3.txt") {
						TextContent = () => "Asset3",
						Encoding = Encoding.ASCII,
					},
				}
			};
			// we don't need any of this stuff!
			assetFeature.Sources.Clear ();
			assetFeature.AndroidResources.Clear ();
			assetFeature.SetProperty ("FeatureType", "AssetPack");

			var feature1 = new XamarinAndroidLibraryProject () {
				ProjectName = "Feature1",
				IsRelease = isRelease,
			};
			feature1.Sources.Clear ();
			feature1.AndroidResources.Clear ();
			// Add an activity which the main app can call.
			feature1.Sources.Add (new BuildItem.Source ("FeatureActivity.cs") {
				TextContent = () => @"using System;
using Android.App;
using Android.Content;
using Android.Runtime;
using Android.Views;
using Android.Widget;
using Android.OS;

namespace Feature1
{
	[Register (""" + feature1.ProjectName + @".FeatureActivity""), Activity (Label = ""Feature1"", MainLauncher = false, Icon = ""@drawable/icon"")]
	public class FeatureActivity : Activity {
		protected override void OnCreate (Bundle bundle)
		{
			base.OnCreate (bundle);
			SetContentView (Resource.Layout.FeatureActivity);
		}
	}
}
",
			});
			feature1.AndroidResources.Add (new AndroidItem.AndroidResource ("Resources\\Layout\\FeatureActivity.xml") {
				TextContent = () => @"<?xml version='1.0' encoding='utf-8' ?>
<LinearLayout xmlns:android='http://schemas.android.com/apk/res/android'
	android:orientation='vertical'
	android:layout_width='fill_parent'
	android:layout_height='fill_parent'>
	<TextView
		android:id='@+id/featuretext'
		android:layout_width='wrap_content'
		android:layout_height='wrap_content'
		android:layout_centerInParent='true'
		android:text='Feature Activity Shown!'
	/>
</LinearLayout>
",
			});
			feature1.SetProperty ("FeatureType", "Feature");
			feature1.SetProperty ("FeatureTitleResource", "@string/feature1");
			var app = new XamarinAndroidApplicationProject {
				ProjectName = "MyApp",
				IsRelease = isRelease,
				OtherBuildItems = {
					new AndroidItem.AndroidResource (() => "Resources\\values\\string1.xml") {
						TextContent = () => @"<resources>
					<string name=""feature1"">Feature1</string>
				</resources>",
					},
				}
			};
			app.SetProperty ("AndroidPackageFormat", "aab");
			var reference = new BuildItem ("ProjectReference", $"..\\{assetFeature.ProjectName}\\{assetFeature.ProjectName}.csproj");
			app.References.Add (reference);
			reference = new BuildItem ("ProjectReference", $"..\\{feature1.ProjectName}\\{feature1.ProjectName}.csproj");
			app.References.Add (reference);
			using (var libBuilder = CreateDllBuilder (Path.Combine (path, assetFeature.ProjectName))) {
				libBuilder.Save (assetFeature);
				using (var libBuilder1 = CreateDllBuilder (Path.Combine (path, feature1.ProjectName))) {
					libBuilder1.Save (feature1);
					using (var appBuilder = CreateApkBuilder (Path.Combine (path, app.ProjectName))) {
						Assert.IsTrue (appBuilder.Build (app), $"{app.ProjectName} should succeed");
						// Check the final aab has the required feature files in it.
						var aab = Path.Combine (Root, appBuilder.ProjectDirectory,
							app.OutputPath, $"{app.PackageName}.aab");
						using (var zip = ZipHelper.OpenZip (aab)) {
							Assert.IsTrue (zip.ContainsEntry ($"feature1/root/assemblies/{feature1.ProjectName}.dll"), $"aab should contain feature1/root/assemblies/{feature1.ProjectName}.dll");
							Assert.IsFalse (zip.ContainsEntry ("feature1/root/assemblies/System.dll"), "aab should not contain feature1/root/assemblies/System.dll");
							Assert.IsFalse (zip.ContainsEntry ("feature1/assets.pb"), "aab should contain feature1/assets.pb");
							Assert.IsTrue (zip.ContainsEntry ("feature1/resources.pb"), "aab should contain feature1/resources.pb");
						}
					}
				}
			}
			Assert.Fail ();
		}
	}
}
