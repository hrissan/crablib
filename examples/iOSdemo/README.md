# How to include in iOS projects

Apple XCode projects are too unstable, so here is no complete project, only relevant parts.

Basically, you need 

1. Add the following definitions to XCode project

```
CRAB_IMPL_CF=1
CRAB_IMPL_COMPILE=1
```

2. Add `crab.cpp` to your project sources 

3. Add `crablib/include` to the include folders.

4. Create `crab::RunLoop` in your main, or in AppDelegate

You can more-or-less freely mix Objective-C and C++, but you will need to rename all `.m` files into `.mm` for that.