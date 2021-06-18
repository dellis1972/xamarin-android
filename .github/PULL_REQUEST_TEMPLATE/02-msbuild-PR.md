## Links to issues or bugs

Link to any github or devdiv issues which have been
raised for the issue this PR fixes.

## Bug or Feature Description

Outline the problem that is being fixed. Include stack traces
and other debug output. If the error was introduced in a previous
commit include the commit hash or a link to the PR which introduced
it.
Outline how this PR fixes the problem.

Imagine you will need to
maintain this code in the future and you have a rubbish memory, be as detailed
as possible.

## CheckList

- [] New Feature has been documented
- [] Error/Warning messages have been localized
- [] Error/Warning messages have been documented
- [] MSBuild Unit Test has been added or existing test updated
- [] New MSBuild Tasks have also have unit tests added
- [] New MSBuild Targets have Inputs/Output specified (give reason in description if not required)
- [] New MSBuild Targets that use a .stamp file use it in the correct manner.

Please read [MSBuildBestPractices](https://github.com/xamarin/xamarin-android/blob/main/Documentation/guides/MSBuildBestPractices.md) and make sure your commit follows these guidelines.