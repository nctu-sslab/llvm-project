This is a simple example demonstrating how to use clang plugin and clang rewriter.

The goal is to rewrite orinal source code, automatically add runtime function call in certain statements.

# Build

1. Have your own llvm/clang
2. Put this project under clang/examples and modify CMakeLists
3. re-cmake and build target 'OpenMPRewrite'
4. You will find OpenMPRewrite.so under $BUILD/lib

Once the plugin is built, you can run it using:
--
Linux:
$ clang -fsyntax-only -Xclang -load -Xclang $BUILD/lib/OpenMPRewrite.so -Xclang -plugin -Xclang omp-rewtr some-file.c
$ clang -fsyntax-only -Xclang -load -Xclang $BUILD/lib/OpenMPRewrite.so -Xclang -plugin -Xclang omp-rewtr -plugin-arg-omp-rewtr help -plugin-arg-omp-rewtr --example-argument some-file.c
// If you use cc1, you have to deal with include path yourself

You will see some-file.c.c under the same directory with some-file.c, along with instrumented function calls.
