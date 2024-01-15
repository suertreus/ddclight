DEPS=sdbus-c++ absl_str_format absl_strings absl_status absl_statusor absl_time absl_span absl_synchronization absl_core_headers absl_any_invocable absl_function_ref wayland-client
HDRS=client.h control-backlight.h control-ddc-i2c.h control.h deleter.h enumerate.h fd-holder.h misc.h output.h server.h state.h
SRCS=control-backlight.cc control.cc control-ddc-i2c.cc ddclight.cc enumerate.cc fd-holder.cc misc.cc output.cc server.cc
OBJS=control-backlight.o control.o control-ddc-i2c.o ddclight.o enumerate.o fd-holder.o misc.o output.o server.o
CXXFLAGS+=-Wno-subobject-linkage -Wno-ignored-attributes -Wno-unknown-warning-option

all: ddclight

format: $(HDRS) $(SRCS)
	clang-format -i --style=Google $^

iwyu: $(SRCS)
	include-what-you-use -Xiwyu --no_comments -Xiwyu --no_fwd_decls -std=c++17 `pkg-config --cflags $(DEPS)` $^

%-client-glue.h: %.xml
	sdbus-c++-xml2cpp $^ --proxy=$@
	clang-format -i --style=Google $@

%-server-glue.h: %.xml
	sdbus-c++-xml2cpp $^ --adaptor=$@
	clang-format -i --style=Google $@

ddclight: $(OBJS)
	$(CXX) $(CXXFLAGS) -std=c++17 `pkg-config --libs $(DEPS)` -o $@ $^

%.o: %.cc
	$(CXX) $(CXXFLAGS) -std=c++17 -c `pkg-config --cflags $(DEPS)` -o $@ $<

ddclight.o: ddclight.cc ddclight-client-glue.h ddclight-server-glue.h
	$(CXX) $(CXXFLAGS) -std=c++17 -c `pkg-config --cflags $(DEPS)` -o $@ $<

clean:
	rm -f *-client-glue.h *-server-glue.h ddclight *.o

install: ddclight ddclight.service ddclight.xml
	install -D $< --target-directory="$(DESTDIR)/usr/bin"
	install -D $<.service --mode=0644 --target-directory="$(DESTDIR)/usr/share/dbus-1/services"
	install -D $<.xml --mode=0644 --target-directory="$(DESTDIR)/usr/share/dbus-1/interfaces"

homedir-install: ddclight ddclight.service ddclight.xml
	install -D $< --target-directory="$(HOME)/bin"
	install -D $<.service --mode=0644 --target-directory="$(or $(XDG_DATA_HOME),$(HOME)/.local/share)/dbus-1/services"
	install -D $<.xml --mode=0644 --target-directory="$(or $(XDG_DATA_HOME),$(HOME)/.local/share)/dbus-1/interfaces"

.PHONY: clean all format iwyu install homedir-install
