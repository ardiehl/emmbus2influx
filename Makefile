# specify what libraries should be static linked (e.g. mparser is not available on RedHat 8)

FORMULASUPPORT = 1
# muParser, formular parser, available on fedora but not on RedHat 8
# when set to 1 make will download and compile muparser
MUPARSERSTATIC = 0

# paho (mqtt) static or dynamic
PAHOSTATIC     = 0

# libcurl static or dynamic, currently (08/2023) raspberry as well as Fedora 38
# have versions installed that does not support websockets
CURLSTATIC = 1

TARGETS = emmbus2influx

# Define a variable to hold the operating system name
OS             := $(shell uname -s)

# OS dependend executables
WGET           = wget
TAR            = tar
MAKEDIR        = mkdir -p
RM             = rm -f
RMRF           = rm -rf
COPY           = cp
ARCH           = $(shell uname -m && mkdir -p obj-`uname -m`/influxdb-post && mkdir -p obj-`uname -m`/libmbus/mbus &&  mkdir -p obj-`uname -m`/ccronexpr)
SUDO           = sudo
INSTALLDIR     = /usr/local
INSTALLDIR_BIN = $(INSTALLDIR)/bin
INSTALLDIR_CFG = $(INSTALLDIR)/etc
INSTALLDIR_SYS = $(INSTALLDIR)/lib/systemd/system
SYSTEMD_RELOAD = systemctl daemon-reload
BZIP2          = bzip2 -d -c
XZUNPACK       = xz -d -c


ALLTARGETS = $(TARGETS:=$(TGT))

CPPFLAGS = -fPIE -g0 -Os -Wall -g -Imqtt$(TGT)/include -Ilibmbus/mbus -Iccronexpr -DCRON_USE_LOCAL_TIME -DSML_NO_UUID_LIB

ifeq ($(OS), FreeBSD)
	CPPFLAGS += -I/usr/local/include
	LDFLAGS += -L/usr/local/lib
	CURLSTATIC = 0
else ifeq ($(OS), Darwin)
	ISYSROOT = /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
	CPPFLAGS += -I/opt/homebrew/include -I/opt/homebrew/opt/curl/include -isysroot $(ISYSROOT)
	LDFLAGS += -L/opt/homebrew/lib -L/opt/homebrew/opt/curl/lib
	CURLSTATIC = 0
endif

# auto generate dependency files
CPPFLAGS += -MMD

.PHONY: default all clean info Debug cleanDebug

default: $(ALLTARGETS)
all: default
Debug: all
cleanDebug: clean

LIBS = -lm -lpthread
OBJDIR       = obj-$(ARCH)$(TGT)
SOURCES      = $(wildcard *.c influxdb-post/*.c libmbus/mbus/*.c *.cpp) ccronexpr/ccronexpr.c
OBJECTS      = $(filter %.o, $(patsubst %.c, $(OBJDIR)/%.o, $(SOURCES)) $(patsubst %.cpp, $(OBJDIR)/%.o, $(SOURCES)))
MAINOBJS     = $(patsubst %, $(OBJDIR)/%.o,$(TARGETS))
LINKOBJECTS  = $(filter-out $(MAINOBJS), $(OBJECTS))
DEPS         = $(OBJECTS:.o=.d)

ifeq ($(PAHOSTATIC),1)
ifeq ($(TGT),-gx)
MQTTLIBDIR   = mqtt-gx/lib
else
MQTTLIBDIR   = mqtt/lib64
endif
MQTTLIB      = libpaho-mqtt3c.a
MQTTLIBP     = $(MQTTLIBDIR)/$(MQTTLIB)
LIBS       += -L./$(MQTTLIBDIR) -l:$(MQTTLIB)
else
LIBS         += -lpaho-mqtt3c
endif


ifeq ($(FORMULASUPPORT),1)
LIBS          += -lreadline
ifeq ($(MUPARSERSTATIC),1)
MUPARSERVERSION= 2.3.3-1
MUPARSERSRCFILE= v$(MUPARSERVERSION).tar.gz
MUPARSERSRC    = https://github.com/beltoforion/muparser/archive/refs/tags/$(MUPARSERSRCFILE)
MUPARSERDIR    = muparser$(TGT)
MUPARSERTAR    = $(MUPARSERDIR)/$(MUPARSERSRCFILE)
MUPARSERMAKEDIR= $(MUPARSERDIR)/muparser-$(MUPARSERVERSION)
MUPARSERMAKE   = $(MUPARSERMAKEDIR)/Makefile
MUPARSERLIB    = $(MUPARSERMAKEDIR)/libmuparser.a
LIBS          += $(MUPARSERLIB)
CPPFLAGS      += -I$(MUPARSERMAKEDIR)/include
else
LIBS          += -lmuparser
endif
endif

ifeq ($(CURLSTATIC),1)
CURLVERSION  = 8.13.0
CURLVERSION2 = $(subst .,_,$(CURLVERSION))
CURLSRCFILE  = curl-$(CURLVERSION).tar.xz
CURLSRC      = https://github.com/curl/curl/releases/download/curl-$(CURLVERSION2)/$(CURLSRCFILE)
CURLDIR      = curl$(ARCH)$(TGT)
CURLTAR      = $(CURLDIR)/$(CURLSRCFILE)
CURLMAKEDIR  = $(CURLDIR)/curl-$(CURLVERSION)
CURLMAKE     = $(CURLMAKEDIR)/Makefile
CURLLIB      = $(CURLMAKEDIR)/lib/.libs/libcurl.a
LIBS         += $(CURLLIB) -lz -lssl -lcrypto -lzstd
CPPFLAGS     += -I$(CURLMAKEDIR)/include -DCURL_STATIC
else
LIBS          += -lcurl
endif


# include dependencies if they exist
-include $(DEPS)

# ------------------------ muparser static ------------------------------------
ifeq ($(MUPARSERSTATIC),1)

$(MUPARSERTAR):
	@$(MAKEDIR) $(MUPARSERDIR)
	@echo "Downloading $(MUPARSERSRC)"
	@cd $(MUPARSERDIR); $(WGET) $(MUPARSERSRC)

$(MUPARSERMAKE):	$(MUPARSERTAR)
	@echo "unpacking $(MUPARSERSRCFILE)"
	@cd $(MUPARSERDIR); $(TAR) x --gunzip < $(MUPARSERSRCFILE);
	@echo "Generating Makefile"
	@cd $(MUPARSERMAKEDIR); cmake . -DENABLE_SAMPLES=OFF -DENABLE_OPENMP=OFF -DENABLE_WIDE_CHAR=OFF -DBUILD_SHARED_LIBS=OFF
	@echo

$(MUPARSERLIB):	$(MUPARSERMAKE)
	@echo "Compiling nuparser"
	@$(MAKE) -j 4 -s -C $(MUPARSERMAKEDIR) muparser
endif



ifeq ($(PAHOSTATIC),1)
$(MQTTLIBP):
	@cd paho; ./buildmqtt || exit 1; cd ..
endif

# ------------------------ libcurl static -----------------------------------
ifeq ($(CURLSTATIC),1)

$(CURLTAR):
	@$(MAKEDIR) $(CURLDIR)
	@echo "Downloading $(CURLSRC)"
	@cd $(CURLDIR); $(WGET) $(CURLSRC)

$(CURLMAKE):        $(CURLTAR)
	@echo "unpacking $(CURLSRCFILE)"
	@cd $(CURLDIR); $(XZUNPACK) $(CURLSRCFILE) | $(TAR) xv
	@echo "Generating Makefile"
	@cd $(CURLMAKEDIR); ./configure --without-psl --disable-file --disable-ldap --disable-ldaps --disable-tftp --disable-dict --without-libidn2 --with-openssl --enable-websockets  --disable-ftp --disable-rtsp --disable-telnet --disable-pop3 --disable-imap --disable-smb --disable-smtp --disable-gopher --disable-mqtt --disable-manual --disable-ntlm --disable-unix-sockets --disable-cookies --without-brotli --without-libpsl --without-nghttp2 --without-nghttp3
	@echo

$(CURLLIB): $(CURLMAKE)
	@echo "Compiling curl"
	@$(MAKE) -s -C $(CURLMAKEDIR)

endif


$(OBJDIR)/%.o: %.c  $(MQTTLIBP) $(MUPARSERLIB) $(CURLLIB)
	@echo -n "compiling $< to $@ "
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@
	@echo ""


$(OBJDIR)/%.o: %.cpp $(MQTTLIBP) $(MUPARSERLIB) $(CURLLIB)
	@echo -n "compiling $< to $@ "
	@$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@
	@echo ""


.PRECIOUS: $(TARGETS) $(ALLOBJECTS)

$(ALLTARGETS): $(OBJECTS) $(SMLLIBP) $(MQTTLIBP) $(MUPARSERLIB) $(CURLLIB)
	@echo -n "linking $@ "
	$(CXX) $(LDFLAGS) $(OBJDIR)/$(patsubst %$(TGT),%,$@).o $(LINKOBJECTS) -Wall $(LIBS) -o $@
	@echo ""


build: clean all

install: $(ALLTARGETS)
	@echo "Installing in $(INSTALLDIR)"
	$(SUDO) $(MAKEDIR) $(INSTALLDIR_BIN)
	$(SUDO) $(MAKEDIR) $(INSTALLDIR_CFG)
	$(SUDO) $(MAKEDIR) $(INSTALLDIR_SYS)
	@echo "stop"
	$(SUDO) systemctl is-active --quiet emmbus2influx && systemctl stop emmbus2influx; sleep 2
	$(SUDO) $(COPY) $(TARGETS) $(INSTALLDIR_BIN)
	$(SUDO) $(COPY) emmbus2influx.service $(INSTALLDIR_SYS)
#	$(SUDO) $(COPY) emmbus2influx.conf $(INSTALLDIR_CFG)
	$(SUDO) $(SYSTEMD_RELOAD)
	$(SUDO) systemctl start emmbus2influx

clean:
	@$(RM) $(OBJECTS) $(TARGETS) $(DEPS) $(MUPARSERLIB) $(MQTTLIBP)
	@cd libmbus; make clean; cd ..
	@echo "cleaned"

distclean:	clean
ifeq ($(MUPARSERSTATIC),1)
	@$(RMRF) $(MUPARSERDIR)
endif
ifeq ($(CURLSTATIC),1)
	@$(RMRF) $(CURLDIR)
endif

	rm -rf $(OBJDIR)
	@echo "cleaned static build dirs"

info:
	@echo "       TARGETS: $(TARGETS)"
	@echo "    ALLTARGETS: $(ALLTARGETS)"
	@echo "       SOURCES: $(SOURCES)"
	@echo "       OBJECTS: $(OBJECTS)"
	@echo "   LINKOBJECTS: $(LINKOBJECTS)"
	@echo "      MAINOBJS: $(MAINOBJS)"
	@echo "          DEPS: $(DEPS)"
	@echo "    CC/CPP/CXX: $(CC)/$(CPP)/$(CXX)"
	@echo "        CFLAGS: $(CFLAGS)"
	@echo "      CPPFLAGS: $(CPPFLAGS)"
	@echo "      CXXFLAGS: $(CXXFLAGS)"
	@echo "          LIBS: $(LIBS)"
	@echo "    MQTTLIBDIR: $(MQTTLIBDIR)"
	@echo "       MQTTLIB: $(MQTTLIB)"
	@echo "    CURLSTATIC: $(CURLSTATIC)"
ifeq ($(CURLSTATIC),1)
	@echo "   CURLVERSION: $(CURLVERSION) ($(CURLVERSION2))"
	@echo "       CURLLIB: $(CURLLIB)"
	@echo "       CURLDIR: $(CURLDIR)"
	@echo "       CURLTAR: $(CURLTAR)"
	@echo "       CURLSRC: $(CURLSRC)"
endif
	@echo "MUPARSERSTATIC: $(MUPARSERSTATIC)"
ifeq ($(MUPARSERSTATIC),1)
	@echo "   MUPARSERLIB: $(MUPARSERLIB)"
	@echo "   MUPARSERTAR: $(MUPARSERTAR)"
endif
	@echo "   INSTALLDIRS: $(INSTALLDIR_BIN) $(INSTALLDIR_CFG) $(INSTALLDIR_SYS)"
	@echo "$(notdir $(MUPARSERSRC))"
