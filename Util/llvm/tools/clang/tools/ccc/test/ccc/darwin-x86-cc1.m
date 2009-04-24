// RUN: xcc -ccc-no-clang -ccc-host-bits 32 -ccc-host-machine i386 -ccc-host-system darwin -ccc-host-release 10.5.0 -### -x objective-c -arch i386 -fmessage-length=0 -Wno-trigraphs -fpascal-strings -fasm-blocks -Os -mdynamic-no-pic -DUSER_DEFINE_0 -fvisibility=hidden -mmacosx-version-min=10.5 -gdwarf-2 -IINCLUDE_PATH_0 -Wall -Wextra -Wno-missing-field-initializers -Wno-unused-parameter -Wno-four-char-constants -Wno-unknown-pragmas -Wno-format-y2k -Wpointer-arith -Wreturn-type -Wwrite-strings -Wswitch -Wcast-align -Wchar-subscripts -Winline -Wnested-externs -Wint-to-pointer-cast -Wpointer-to-int-cast -Wshorten-64-to-32 -FFRAMEWORK_0 -IINCLUDE_PATH_1 -FFRAMEWORK_1 -include USER_INCLUDE_0 -c %s -o %t.out &> %t.opts &&
// RUN: grep ' "/usr/libexec/gcc/i686-apple-darwin10/4.2.1/cc1obj" "-quiet" "-IINCLUDE_PATH_0" "-FFRAMEWORK_0" "-IINCLUDE_PATH_1" "-FFRAMEWORK_1" "-D__DYNAMIC__" "-DUSER_DEFINE_0" "-include" "USER_INCLUDE_0" ".*" "-quiet" "-dumpbase" "darwin-x86-cc1.m" "-mpascal-strings" "-mdynamic-no-pic" "-mmacosx-version-min=10.5" "-mtune=core2" "-auxbase-strip" ".*" "-gdwarf-2" "-Os" "-Wno-trigraphs" "-Wall" "-Wextra" "-Wno-missing-field-initializers" "-Wno-unused-parameter" "-Wno-four-char-constants" "-Wno-unknown-pragmas" "-Wno-format-y2k" "-Wpointer-arith" "-Wreturn-type" "-Wwrite-strings" "-Wswitch" "-Wcast-align" "-Wchar-subscripts" "-Winline" "-Wnested-externs" "-Wint-to-pointer-cast" "-Wpointer-to-int-cast" "-Wshorten-64-to-32" "-fmessage-length=0" "-fasm-blocks" "-fvisibility=hidden" "-o"' %t.opts &&
// RUN: grep ' "/usr/libexec/gcc/i686-apple-darwin10/4.2.1/as" "-arch" "i386" "-force_cpusubtype_ALL" "-o"' %t.opts &&

// RUN: xcc -ccc-no-clang -ccc-host-bits 32 -ccc-host-machine i386 -ccc-host-system darwin -ccc-host-release 10.5.0 -### -v -E -dM -arch i386 -xobjective-c -c %s &> %t.opts &&
// RUN: grep ' "/usr/libexec/gcc/i686-apple-darwin10/4.2.1/cc1obj" "-E" "-quiet" "-v" "-D__DYNAMIC__" ".*" "-fPIC" "-mmacosx-version-min=10.6.5" "-mtune=core2" "-dM"' %t.opts && 

// RUN: xcc -ccc-no-clang -ccc-host-bits 32 -ccc-host-machine i386 -ccc-host-system darwin -ccc-host-release 10.5.0 -### -m32 -S -x cpp-output %s &> %t.opts &&
// RUN: grep ' "/usr/libexec/gcc/i686-apple-darwin10/4.2.1/cc1" "-fpreprocessed" ".*darwin-x86-cc1.m" "-fPIC" "-quiet" "-dumpbase" "darwin-x86-cc1.m" "-mmacosx-version-min=10.6.5" "-m32" "-mtune=core2" "-auxbase" "darwin-x86-cc1" "-o" ".*"' %t.opts &&

// RUN: xcc -ccc-no-clang -ccc-host-bits 32 -ccc-host-machine i386 -ccc-host-system darwin -ccc-host-release 10.5.0 -### -x objective-c-header %s -o /tmp/x.gch &> %t.opts &&
// RUN: grep ' "/usr/libexec/gcc/i686-apple-darwin10/4.2.1/cc1obj" "-quiet" "-D__DYNAMIC__" ".*darwin-x86-cc1.m" "-fPIC" "-quiet" "-dumpbase" "darwin-x86-cc1.m" "-mmacosx-version-min=10.6.5" "-mtune=core2" "-auxbase" ".*" "-o" "/dev/null" "--output-pch=" "/tmp/x.gch"' %t.opts &&

// RUN: true

