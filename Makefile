DEPS="sdbus-c++ absl_str_format absl_strings absl_status absl_statusor absl_time absl_span absl_synchronization absl_core_headers absl_any_invocable absl_function_ref"

all: ddclight

format: ddclight.cc client.h server.h ddc.h ddc.cc enumerate.h enumerate.cc thread.h thread.cc state.h
	clang-format -i --style=Google $^

iwyu:
	include-what-you-use -Xiwyu --no_comments -Xiwyu --no_fwd_decls -std=c++17 ddclight.cc `pkgconf --cflags $(DEPS)`
	include-what-you-use -Xiwyu --no_comments -Xiwyu --no_fwd_decls -std=c++17 ddc.cc `pkgconf --cflags $(DEPS)`
	include-what-you-use -Xiwyu --no_comments -Xiwyu --no_fwd_decls -std=c++17 enumerate.cc `pkgconf --cflags $(DEPS)`
	include-what-you-use -Xiwyu --no_comments -Xiwyu --no_fwd_decls -std=c++17 thread.cc `pkgconf --cflags $(DEPS)`

%-client-glue.h: %.xml
	sdbus-c++-xml2cpp $^ --proxy=$@
	clang-format -i --style=Google $@

%-server-glue.h: %.xml
	sdbus-c++-xml2cpp $^ --adaptor=$@
	clang-format -i --style=Google $@

ddclight: ddclight.o ddc.o enumerate.o thread.o
	$(CXX) $(CXXFLAGS) -std=c++17 -o $@ $^ `pkgconf --libs $(DEPS)`

ddclight.o: ddclight.cc ddclight-client-glue.h ddclight-server-glue.h
	$(CXX) $(CXXFLAGS) -std=c++17 -c -o $@ $< `pkgconf --cflags $(DEPS)`

ddc.o: ddc.cc
	$(CXX) $(CXXFLAGS) -std=c++17 -c -o $@ $< `pkgconf --cflags $(DEPS)`

enumerate.o: enumerate.cc
	$(CXX) $(CXXFLAGS) -std=c++17 -c -o $@ $< `pkgconf --cflags $(DEPS)`

thread.o: thread.cc
	$(CXX) $(CXXFLAGS) -std=c++17 -c -o $@ $< `pkgconf --cflags $(DEPS)`

clean:
	rm -f *-client-glue.h *-server-glue.h ddclight *.o

install: ddclight ddclight.service ddclight.xml
	install -D $< --target-directory="$(DESTDIR)/usr/bin"
	install -D $<.service --mode=0644 --target-directory="$(DESTDIR)/usr/share/dbus-1/services"
	install -D $<.xml --mode=0644 --target-directory="$(DESTDIR)/usr/share/dbus-1/interfaces"

.PHONY: clean all format iwyu install
