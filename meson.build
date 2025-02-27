project('naev', 'c',
   version : '0.9.0-beta.2',
   default_options : [
      'warning_level=2',
      'optimization=g',
      'c_std=gnu11',
      'werror=false'
   ],
   meson_version: '>=0.55.0')

issue_address = 'https://github.com/naev/naev/issues'
copyright_holder = 'Naev Dev Team'

# Tools
cc = meson.get_compiler('c')
foreach arg: ['-Wextra', '-Wshadow']
   if cc.has_argument(arg)
      add_project_arguments(arg, language : 'c')
   endif
endforeach

# Python module checks (For AUTHORS generation and soundtrack generation)
python_module = import('python')
python = python_module.find_installation('python3', required: true)
pyyaml = python_module.find_installation('python3', modules: ['yaml'], required: true)
mutagen = python_module.find_installation('python3', modules: ['mutagen'], required: false)

cc = meson.get_compiler('c')
subdir('utils')

# Version Generation
version_result = run_command(gen_version, meson.project_version())
if version_result.returncode() != 0
   error(version_result.stderr())
else
   version = version_result.stdout().strip()
   meson.add_dist_script(add_to_package, 'dat/VERSION')
   summary('tag', version)
endif

# Initialization
config_data = configuration_data()
naev_deps = []

ndata_path = get_option('ndata_path')
if ndata_path == ''
   ndata_path = get_option('datadir') / 'naev'
endif
summary('NData Path', ndata_path, section: 'Features')

# Subdirs
subdir('src')
subdir('dat/scripts')
subdir('docs')
subdir('po')

####
# Naev
####
buildExec = get_option('executable')
if buildExec.disabled() == false
   debug = get_option('debug')
   debug_arrays = get_option('debug_arrays')
   paranoid = get_option('paranoid')
   config_data.set_quoted('PACKAGE', meson.project_name())
   config_data.set_quoted('PACKAGE_NAME', meson.project_name())
   config_data.set_quoted('PACKAGE_VERSION', meson.project_version())
   config_data.set_quoted('PKGDATADIR', get_option('prefix') / ndata_path)
   config_data.set_quoted('HOST', host_machine.system() + '-' + host_machine.cpu_family())
   # Cut out the numeric fields from our SemVer <major>.<minor>.<rev>[-pre_release][+build] for OS versioning.
   config_data.set('VMAJOR', meson.project_version().split('.')[0])
   config_data.set('VMINOR', meson.project_version().split('.')[1])
   config_data.set('VREV', meson.project_version().split('.')[2].split('-')[0].split('+')[0])
   config_data.set('DEBUG', debug ? 1 : false)
   config_data.set('DEBUG_ARRAYS', debug_arrays ? 1 : false)
   config_data.set('DEBUGGING', debug ? 1 : false)
   config_data.set('DEBUG_PARANOID', paranoid ? 1 : false)
   summary('Enabled' , debug   , section: 'Debug', bool_yn: true)
   summary('Paranoid', paranoid, section: 'Debug', bool_yn: true)

   ### Hard deps (required: true)

   naev_deps += cc.find_library('m', required : false)

   sdl = dependency('sdl2', required: true)
   sdl_image = dependency('SDL2_image', required: true, fallback: ['sdl2_image', 'sdl2_image_dep'])
   libunibreak = dependency('libunibreak', required: true, version: '>=4.0', fallback: ['libunibreak', 'libunibreak_dep'])

   libxml2 = dependency('libxml-2.0', required: false)
   if not libxml2.found()
      libxml2 = cc.find_library('xml2', required: true)  # e.g., MacOSX SDK
   endif

   use_system_physfs = ('physfs' not in get_option('force_fallback_for') and 'forcefallback' != get_option('wrap_mode'))
   if use_system_physfs
      system_physfs = dependency('physfs', required: false)
      if not system_physfs.found()
         system_physfs = cc.find_library('physfs', required: false, has_headers: ['physfs.h'])
      endif
      use_system_physfs = system_physfs.found()
   endif
   naev_deps += use_system_physfs ? system_physfs : subproject('physfs').get_variable('physfs_dep')

   naev_deps += [
      dependency('freetype2', required: true),
      libunibreak,
      libxml2,
      sdl,
      sdl_image,
      declare_dependency(
         dependencies: dependency('libpng', required: true),
         compile_args: '-DNOLOGPRINTFCONSOLE',
         link_args: '-DNOLOGPRINTFCONSOLE')
   ]

   naev_deps += dependency('libwebp', required: true, static: get_option('steamruntime'))

   # Lua
   useLuaJIT = get_option('luajit')
   lua = dependency('', required: false)
   if useLuaJIT.disabled() == false
      lua = dependency('luajit', fallback: ['luajit', 'luajit_dep'], required: useLuaJIT)
   endif
   config_data.set10('HAVE_LUAJIT', lua.found())
   summary('LuaJIT', lua.found(), section: 'Features', bool_yn: true)

   if not lua.found()
      lua = dependency('lua51', fallback: ['lua', 'lua_dep'], required: true)
   endif

   naev_deps += lua

   # Linear Algebra
   use_system_glpk = ('glpk' not in get_option('force_fallback_for') and 'forcefallback' != get_option('wrap_mode'))
   use_system_suitesparse = ('SuiteSparse' not in get_option('force_fallback_for') and 'forcefallback' != get_option('wrap_mode'))

   if use_system_glpk
      system_glpk = cc.find_library('glpk', required: false, has_headers: ['glpk.h'])
      use_system_glpk = system_glpk.found()
   endif
   naev_deps += use_system_glpk ? system_glpk : subproject('glpk').get_variable('glpk_dep')

   if use_system_suitesparse
      message('Note: Naev needs the following SuiteSparse libraries: amd, colamd, cholmod, and either csparse or cxsparse. ' +
              'It can use its own copies if you run "meson configure --force-fallback-for=SuiteSparse".')
      foreach csparse_name : ['csparse', 'cxsparse']
         system_csparse = cc.find_library(csparse_name, required: false)
         if system_csparse.found()
            break
         endif
      endforeach
      if system_csparse.found()
         naev_deps += system_csparse
         config_data.set10('HAVE_SUITESPARSE_CS_H', cc.has_header('suitesparse/cs.h'))
      else
         error('Failed to find an installation of c[x]sparse. See above note about SuiteSparse.')
      endif

      system_cholmod = cc.find_library('cholmod', required: false)
      if system_cholmod.found()
         naev_deps += system_cholmod
         config_data.set10('HAVE_SUITESPARSE_CHOLMOD_H', cc.has_header('suitesparse/cholmod.h'))
      else
         error('Failed to find an installation of cholmod. See above note about SuiteSparse.')
      endif

      naev_deps += cc.find_library('amd', required: true)
      naev_deps += cc.find_library('ccolamd', required: false)
      naev_deps += cc.find_library('colamd', required: true)
      naev_deps += cc.find_library('lapack', required: false)
      naev_deps += cc.find_library('metis', required: false)
      naev_deps += cc.find_library('suitesparseconfig', required: true)
   else
      naev_deps += subproject('SuiteSparse').get_variable('SuiteSparse_dep')
   endif

   if get_option('steamruntime')
      blas = cc.find_library('openblas', required: true, static: true)
   elif get_option('blas') == 'Accelerate'
      blas = dependency('Accelerate', required: true)
   else
      blas = cc.find_library(get_option('blas'), required: true)
   endif

   # It would be nice to detect the functions and just embed the prototypes. Problem is, we might not know the int width to use...
   #config_data.set10('HAVE_BLAS_CBLAS_FUNC', cc.has_function('cblas_ddot', dependencies: blas))
   #config_data.set10('HAVE_BLAS_FUNC_', cc.has_function('ddot_', dependencies: blas))
   naev_deps += blas

   ### Soft deps (required: false)

   # BFD
   if get_option('debug')
      bfd = cc.find_library('bfd', required: false)
   else
      bfd = dependency('', required: false)
   endif
   summary('BFD', bfd.found(), section: 'Debug', bool_yn: true)

   naev_deps += bfd
   config_data.set10('HAS_BFD', bfd.found())

   # Appstream (Used for generating desktop files and verifying metadata)
   ascli_exe = find_program('appstreamcli', version: '>=0.12.9', required: false)

   # Luacheck (linter) wrapper
   luacheckIfInstalled = find_program('utils/luacheckIfInstalled.sh')

   # Audio
   openal    = dependency('openal', required: true)
   vorbis = dependency('vorbis', required: true)
   vorbisfile = dependency('vorbisfile', required: true)
   ogg = dependency('ogg', required: true)  # Transitive dependency. At least some MSYS2 build envs may miss it.

   naev_deps += [openal, ogg, vorbis, vorbisfile]

   # Standard library feature tests
   config_data.set10('HAVE_ASPRINTF', cc.has_function('asprintf'))
   config_data.set10('HAVE_DECL_SETENV', cc.has_header_symbol('stdlib.h', 'setenv'))
   config_data.set10('HAVE_DECL__PUTENV_S', cc.has_header_symbol('stdlib.h', '_putenv_s'))
   config_data.set10('HAVE_DECL_GETENV_S', cc.has_header_symbol('stdlib.h', 'getenv_s'))
   config_data.set10('HAVE_FEENABLEEXCEPT', cc.has_header_symbol('fenv.h', 'feenableexcept', prefix: '#define _GNU_SOURCE'))
   config_data.set10('HAVE_ALLOCA_H', cc.has_header('alloca.h'))
   config_data.set10('HAVE_FENV_H', cc.has_header('fenv.h'))
   config_data.set10('HAVE_MALLOC_H', cc.has_header('malloc.h'))
   config_data.set10('HAVE_STRCASESTR', cc.has_function('strcasestr'))
   # strndup() detectin must work around this bug: https://github.com/mesonbuild/meson/issues/3672
   config_data.set10('HAVE_STRNDUP', cc.has_header_symbol('string.h', 'strndup') and cc.has_function('strndup'))
   config_data.set10('HAVE_STRNSTR', cc.has_function('strnstr'))
   config_data.set10('HAVE_VASPRINTF', cc.has_function('vasprintf'))
   # BLAS include tests
   cblas_test = '\nint main (void) { double x = 0; return (int)cblas_ddot( 1, &x, 1, &x, 1 ); }'
   config_data.set10('HAVE_ACCELERATE_ACCELERATE_H', cc.links('#include <Accelerate/Accelerate.h>' + cblas_test, dependencies: blas))
   config_data.set10('HAVE_CBLAS_H', cc.links('#include <cblas.h>' + cblas_test, dependencies: blas))
   config_data.set10('HAVE_CBLAS_OPENBLAS_H', cc.links('#include <cblas_openblas.h>' + cblas_test, dependencies: blas))
   config_data.set10('HAVE_CBLAS_HYPHEN_OPENBLAS_H', cc.links('#include <cblas-openblas.h>' + cblas_test, dependencies: blas))
   config_data.set10('HAVE_OPENBLAS_CBLAS_H', cc.links('#include <openblas/cblas.h>' + cblas_test, dependencies: blas))
   config_data.set10('HAVE_F77BLAS_H', cc.has_header('f77blas.h', dependencies: blas))

   # OS declaration (Shows which OS we built this with in the options menu)
   if host_machine.system() == 'linux'
      config_data.set10('LINUX', true)
   elif host_machine.system() == 'freebsd'
      config_data.set10('FREEBSD', true)
   elif host_machine.system() == 'darwin'
      config_data.set10('MACOS', true)
   elif host_machine.system() == 'windows'
      config_data.set10('WIN32', true)
   endif

   ### Generated sources

   # VERSION
   config_data.set_quoted('VERSION', version)

   # config.h
   configure_file(
      output: 'config.h',
      configuration: config_data
   )
   add_project_arguments('-include', 'config.h', language: 'c')

   # GLAD
   if not cc.has_header('windows.h')
      naev_deps += cc.find_library('dl', required: true)
   endif

   include_dirs = [include_directories(
      'src',
      'src/tk',
      'src/tk/widget'
   )]

   libsdf = static_library('sdf', sdf_source, include_directories: include_dirs, override_options: ['optimization=3'])
   naev_deps += declare_dependency(link_with: libsdf)

   if host_machine.system() == 'darwin'
      add_languages('objc', native: false)
      configure_file(input: 'extras/macos/Info.plist.in', output: 'Info.plist', configuration: config_data,
         install: true, install_dir: 'Contents')
      install_data('extras/logos/naev.icns', install_dir: ndata_path)
      naev_source += mac_source
      naev_deps += dependency('Foundation', required: true )
   endif

   if host_machine.system() == 'windows'
      windows = import('windows')
      icon = files('extras/logos/logo.ico')
      install_data(icon, install_dir: '.')
      res_include = include_directories('extras/logos')
      win_manifest = configure_file(input: 'extras/windows/naev.exe.manifest.in', output: 'naev.exe.manifest', configuration: config_data)
      win_rc = configure_file(input: 'extras/windows/resource.rc.in', output: 'resource.rc', configuration: config_data)
      naev_source += windows.compile_resources(win_rc, depend_files: [win_manifest, icon], include_directories: res_include)
   endif

   shaders_source = custom_target(
      'generate_shaders',
      command: [python, '@INPUT@'],
      input: 'src/shaders_c_gen.py',
      output: ['shaders.gen.c', 'shaders.gen.h']
   )
   naev_source += shaders_source
   colours_source = custom_target(
      'generate_colours',
      command: [python, '@INPUT@'],
      input: 'src/colours_c_gen.py',
      output: ['colours.gen.c', 'colours.gen.h']
   )
   naev_source += colours_source

   naev_bin = executable(
      'naev',
      naev_source,
      include_directories: include_dirs,
      dependencies: naev_deps,
      export_dynamic: bfd.found(),
      install: true)

   gen_authors = find_program('utils/build/gen_authors.py')
   authors = custom_target(
      'authors',
      command: [
         gen_authors,
         '--output', '@OUTPUT0@',
         '--preamble', '@INPUT0@',
         '@INPUT1@', '@INPUT2@',
      ],
      input: files('dat/AUTHORS', 'artwork/gfx/ARTWORK_LICENSE.yaml', 'artwork/snd/SOUND_LICENSE.yaml'),
      output: 'AUTHORS',
      install: true,
      install_dir: ndata_path / 'dat',
   )

   gen_gettext_stats = find_program('utils/build/gen_gettext_stats.py')
   gettext_stats = custom_target(
      'gettext_stats',
      command: [gen_gettext_stats, '--output', '@OUTPUT@', '@INPUT@'],
      input: files('po/' + meson.project_name() + '.pot'),
      output: meson.project_name() + '.txt',
      install: true,
      install_dir: ndata_path / 'dat/gettext_stats',
   )

   naev_sh = configure_file(
      input: 'utils/build/naev.sh',
      output: 'naev.sh',
      configuration: {
         'build_root': meson.current_build_dir(),
         'source_root': meson.source_root(),
         'naev_bin' : naev_bin.full_path(),
         'authors' : authors.full_path(),
         'gettext_stats' : gettext_stats.full_path(),
      }
   )

   if host_machine.system() not in ['windows', 'darwin']
      install_data(
         'gpl.txt',
         'LICENSE',
         'Readme.md',
         install_dir: get_option('datadir') / 'doc/naev'
      )
   endif
   # TODO: And what if it is 'windows' or 'darwin'?

   install_subdir(
      'dat',
      install_dir: ndata_path,
      exclude_files: 'AUTHORS',  # In favor of the above "authors" target.
   )

   install_subdir(
      'artwork',
      install_dir: ndata_path / 'dat',
      exclude_directories: '.git',  # That's a marker used by "git submodule".
      strip_directory: true,
   )

   if host_machine.system() not in ['windows', 'cygwin', 'emscripten', 'android', 'darwin']
      metainfo_file = 'org.naev.naev.metainfo.xml'

      if (ascli_exe.found())
         # Create desktop-entry file from metainfo
         custom_target('gen-desktop-entry',
            input : [metainfo_file],
            output : ['org.naev.naev.desktop'],
            command : [ascli_exe, 'make-desktop-file', '@INPUT@', '@OUTPUT@'],
            install: true,
            install_dir: join_paths (get_option ('datadir'), 'applications')
         )
      else
         install_data('org.naev.naev.desktop', install_dir: join_paths (get_option ('datadir'), 'applications'))
      endif

      install_data(metainfo_file, install_dir: get_option('datadir') / 'metainfo')

      install_data(['extras/logos/logo16.png', 'extras/logos/logo32.png', 'extras/logos/logo64.png', 'extras/logos/logo128.png', 'extras/logos/naev.png'],
             rename : ['16x16/apps/naev.png', '32x32/apps/naev.png', '64x64/apps/naev.png', '128x128/apps/naev.png', '256x256/apps/naev.png'],
             install_dir: get_option('datadir') / 'icons/hicolor')
      install_man('naev.6')
   endif

   subdir('test')

   if host_machine.system() == 'windows'
     meson.add_install_script('extras/windows/packageWindows.sh', '-n', get_option('nightly').to_string())
   endif
   if host_machine.system() == 'darwin'
     meson.add_install_script('extras/macos/bundle.py')
   endif
endif

####
# Soundtrack
####
if (mutagen.found())
   soundtrackpy = find_program('utils/soundtrack.py')
   custom_target(
      'soundtrack',
      command: [
         soundtrackpy,
         '--csv', 'yes',
         '--source-dir', meson.source_root(),
         '--output', '@OUTPUT0@'
      ],
      output: [
         'naev-' + meson.project_version() + '-soundtrack.zip',
         'naev-' + meson.project_version() + '-soundtrack.csv'
      ]
   )
endif
