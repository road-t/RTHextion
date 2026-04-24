QT += testlib widgets concurrent
CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = tst_hexdocument

SOURCES += tst_hexdocument.cpp \
    ../../src/document/hexdocument.cpp \
    ../../src/document/translationtable.cpp \
    ../../src/document/PointerListModel.cpp \
    ../../src/document/SectionListModel.cpp \
    ../../src/hexeditor/hexeditor.cpp \
    ../../src/hexeditor/encoding.cpp \
    ../../src/hexeditor/events.cpp \
    ../../src/hexeditor/disasm.cpp \
    ../../src/hexeditor/pointers.cpp \
    ../../src/hexeditor/scrollmap.cpp \
    ../../src/hexeditor/layout.cpp \
    ../../src/hexeditor/graphics.cpp \
    ../../src/hexeditor/chunks.cpp \
    ../../src/hexeditor/commands.cpp \
    ../../src/hexeditor/hexscrollmap.cpp \
    ../../src/utils/disassembler.cpp \
    ../../src/utils/appsettings.cpp \
    ../../src/audio/audiodetector.cpp

HEADERS += \
    ../../src/document/hexdocument.h \
    ../../src/document/translationtable.h \
    ../../src/document/PointerListModel.h \
    ../../src/document/SectionListModel.h \
    ../../src/hexeditor/hexeditor.h \
    ../../src/hexeditor/internal.h \
    ../../src/hexeditor/encoding.h \
    ../../src/hexeditor/chunks.h \
    ../../src/hexeditor/commands.h \
    ../../src/hexeditor/hexscrollmap.h \
    ../../src/audio/audiodetector.h \
    ../../src/utils/disassembler.h \
    ../../src/utils/Datas.h \
    ../../src/utils/romdetect.h

macx: LIBS += -liconv

capstone_enabled = false

!equals(capstone_enabled, true):macx {
    capstone_target_archs = $$QMAKE_APPLE_DEVICE_ARCHS
    isEmpty(capstone_target_archs) {
        capstone_target_archs = $$system("/usr/bin/uname -m")
    }

    capstone_bundled_lib = $$PWD/../../src/libs/capstone/libcapstone.a
    capstone_archs = $$system("/usr/bin/lipo -archs $$shell_path($$capstone_bundled_lib) 2>/dev/null")

    capstone_bundled_ok = true
    isEmpty(capstone_archs) {
        capstone_bundled_ok = false
    }
    equals(capstone_bundled_ok, true) {
        for(cap_arch, capstone_target_archs) {
            !contains(capstone_archs, $$cap_arch) {
                capstone_bundled_ok = false
            }
        }
    }

    equals(capstone_bundled_ok, true) {
        capstone_enabled = true
        INCLUDEPATH += $$PWD/../../src/libs/capstone/include
        LIBS += -L$$PWD/../../src/libs/capstone -lcapstone
    }
}

!equals(capstone_enabled, true):macx {
    capstone_brew_prefix = $$system("brew --prefix capstone 2>/dev/null")
    !isEmpty(capstone_brew_prefix) {
        capstone_target_archs = $$QMAKE_APPLE_DEVICE_ARCHS
        isEmpty(capstone_target_archs) {
            capstone_target_archs = $$system("/usr/bin/uname -m")
        }

        capstone_brew_lib = $$capstone_brew_prefix/lib/libcapstone.dylib
        !exists($$capstone_brew_lib) {
            capstone_brew_lib = $$capstone_brew_prefix/lib/libcapstone.a
        }
        capstone_brew_archs = $$system("/usr/bin/lipo -archs $$shell_path($$capstone_brew_lib) 2>/dev/null")

        capstone_brew_ok = true
        isEmpty(capstone_brew_archs) {
            capstone_brew_ok = false
        }
        equals(capstone_brew_ok, true) {
            for(cap_arch, capstone_target_archs) {
                !contains(capstone_brew_archs, $$cap_arch) {
                    capstone_brew_ok = false
                }
            }
        }

        equals(capstone_brew_ok, true) {
            capstone_enabled = true
            INCLUDEPATH += $$capstone_brew_prefix/include
            LIBS += -L$$capstone_brew_prefix/lib -lcapstone
        }
    }
}

!equals(capstone_enabled, true):!macx {
    capstone_pkg = $$system("pkg-config --exists capstone && echo yes")
    contains(capstone_pkg, yes) {
        capstone_enabled = true
        CONFIG += link_pkgconfig
        PKGCONFIG += capstone
    }
}

equals(capstone_enabled, true) {
    DEFINES += HAVE_CAPSTONE
} else {
    warning("Capstone not found for tst_hexdocument; disassembly-dependent code will be disabled")
}

INCLUDEPATH += ../../src ../../src/hexeditor ../../src/document ../../src/utils ../../src/audio
DEFINES += HEXEDITOR_EXPORTS
