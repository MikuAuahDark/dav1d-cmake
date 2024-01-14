# CMake script to compile dav1d

This is a wrapper for dav1d (AV1 video fromat decoder), which allows to build and link it within the CMake build system. The dav1d's own build system is Meson; this repository wraps over the official dav1d repository to offer the CMake build system integration as well.

## Usage

Typically you may wish to add this repository as a submodule to your CMake project:

```
mkdir ThirdParty
cd ThirdParty
git submodule add https://path/to/this/repo
```

Then, in `CMakeLists.txt` your own CMake targets can link against the dav1d library (and use `dav1d.h` C include header), after adding the submodule directory:

```cmake
add_subdirectory(ThirdParty/dav1d-cmake)

...

add_executable(your_own_project ...)
target_link_libraries(your_own_project dav1d::dav1d)
```

