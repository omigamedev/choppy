#!/bin/bash
mkdir -p package/libs
find build/modules/ -name *.so -not -name *.pcm -exec cp {} package/libs/ \;
find build/_deps/ -name *.so* -not -name *.pcm -exec cp {} package/libs/ \;
cp build/ce_linux package/
tar -czvf linux_package.tar.gz -C package .
rm -rf package
