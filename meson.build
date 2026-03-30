project('amtterm', 'c', default_options : [ 'c_std=c99' ] )

# warnings
add_global_arguments('-Wno-pointer-sign', language : 'c')
add_global_arguments('-D_DEFAULT_SOURCE=1', language : 'c')
add_global_arguments('-D_POSIX_C_SOURCE=200809L', language : 'c')

# config, initialize
config  = configuration_data()
version = run_command('cat', 'VERSION', check: true)
config.set_quoted('VERSION', version.stdout().strip())

# pkg-config deps
glib_dep = dependency('glib-2.0', method: 'pkg-config')
gtk_dep  = dependency('gtk+-3.0', method: 'pkg-config')
gdk_dep  = dependency('gdk-3.0',  method: 'pkg-config')
vte_dep  = dependency('vte-2.91', method: 'pkg-config')

# use gnutls
ssl_dep  = dependency('gnutls',   method: 'pkg-config')
config.set('USE_GNUTLS', true)

# config, finalize
configure_file(output : 'config.h', configuration : config)
add_global_arguments(['-include', 'config.h'], language : 'c')

common_src = ['redir.c', 'tcp.c', 'auth.c', 'ssl.c', 'ider.c']

executable('amtterm',
           sources      : [ 'amtterm.c', common_src ],
           dependencies : [ ssl_dep ],
           install      : true)
executable('amtider',
           sources      : [ 'amtider.c', common_src ],
           dependencies : [ ssl_dep ],
           install      : true)

install_data('amttool',
             install_dir  : get_option('bindir'),
             install_mode : 'rwxr-xr-x')

install_man('amtterm.1')
install_man('amtider.1')
install_man('amttool.1')
install_man('amt-howto.7')

if gtk_dep.found() and vte_dep.found()
    executable('gamt',
               sources      : [ 'gamt.c', 'parseconfig.c', common_src ],
               dependencies : [ gtk_dep, vte_dep, ssl_dep ],
               install      : true)
    install_man('gamt.1')
endif
