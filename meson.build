project('minivcmouse', 'c',
  version : '1.0',
  default_options : ['warning_level=3',
                     'cpp_std=c++14'])

deps = [
    dependency('libudev'),
    dependency('libinput')
]

#ide:editable-filelist
sources = [
  'minivcmouse.c',
]

exe = executable('minivcmouse', sources, dependencies: deps, install : true)

test('basic', exe)
