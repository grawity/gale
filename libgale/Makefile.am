## Process this file with automake to generate Makefile.in

lib_LTLIBRARIES = libgale.la
noinst_PROGRAMS = crypto_test key_test

# version:revision:age
# current as of 0.99egg
libgale_la_LDFLAGS = -version-info 12:0:0

noinst_HEADERS = client_i.h crypto_i.h key_i.h io.h

libgale_la_SOURCES = \
    core_init.c core_link.c core_signals.c \
    io_input.c io_output.c \
    client_alias.c client_code.c client_default.c client_i.c client_location.c \
    client_pack.c client_queue.c client_server.c client_standard.c \
    client_unpack.c \
    crypto_gen.c crypto_i.c crypto_misc.c crypto_seal.c \
    crypto_sign.c crypto_sign_raw.c \
    key_assert.c key_generate.c key_graph.c key_handle.c key_i.c \
    key_search.c key_search_akd.c key_search_builtin.c key_search_dirs.c \
    misc_alloc.c misc_charset.c misc_connect.c misc_debug.c \
    misc_dir.c misc_envvar.c misc_error.c misc_exec.c misc_file.c \
    misc_fragment.c misc_globals.c misc_kill.c misc_map.c misc_pack.c \
    misc_readline.c misc_report.c misc_terminal.c misc_text.c misc_time.c \
    wcwidth.c

crypto_test_SOURCES = crypto_test.c
crypto_test_LDADD = $(GALE_LIBS)

key_test_SOURCES = key_test.c
key_test_LDADD = $(GALE_LIBS)
