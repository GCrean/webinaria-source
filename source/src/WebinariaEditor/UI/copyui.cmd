@echo off

pushd

cd %1

call common\UI\copyui.cmd

md ..\bin\UI\Editor

xcopy WebinariaEditor\UI\*.* ..\bin\UI\Editor /S /Y /EXCLUDE:common\UI\exclude.files

popd

exit 0

