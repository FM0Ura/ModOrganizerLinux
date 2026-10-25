/* Test helper: runs a script with the Python of the AppDir. The AppDir ships libpython
 * (MO2 embeds it) but no interpreter, so this is the smallest interpreter there is:
 *
 *   gcc -o pyrun pyrun.c $(pkg-config --cflags --libs python3-embed)
 *
 * It is not part of the AppImage; run-distro-smoke.sh compiles it once (in the build
 * image) and runs it with LD_LIBRARY_PATH and PYTHONHOME pointing into the AppDir. */
#include <Python.h>

int main(int argc, char** argv)
{
  return Py_BytesMain(argc, argv);
}
