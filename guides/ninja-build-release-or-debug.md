RELEASE-------------------
### \#\# Step 1: Configure for Ninja (Run This Once)

First, you must create a new build directory. We'll call it `juce/build-ninja-release` to keep it separate.

Run this command from your project's root directory (`Pikon-Raditsz`):

```powershell
# This command creates the new build directory for Ninja
cmake -S juce -B juce/build-ninja-release -G Ninja -DCMAKE_BUILD_TYPE=Release
```

  * `-S juce` points to your `juce` source directory.
  * `-B juce/build-ninja-release` is the **new build directory**.
  * `-G Ninja` tells CMake to generate `Ninja` files.
  * `-DCMAKE_BUILD_TYPE=Release` sets the build type. You *must* do this now, as Ninja is a single-configuration generator.

-----

### \#\# Step 2: Build with Ninja (Your New Command)

Now that you have a Ninja build directory, you can run your build command. Notice the `--config Release` flag is **gone**.

```powershell
# This is your new build command
cmake --build juce/build-ninja-release --target PresetCreatorApp
```

`Ninja` will automatically use all your CPU cores to build in parallel, making it very fast.

-----







### Debug


### \#\# Step 1: Configure for Debug (Run This Once)

Run this from your project's root directory (`Pikon-Raditsz`):

```powershell
# We use a new directory "build-ninja-debug"
# We set CMAKE_BUILD_TYPE to Debug
cmake -S juce -B juce/build-ninja-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

-----

### \#\# Step 2: Build for Debug (Your New Command)

Now you just point the build command to your new `juce/build-ninja-debug` directory.

```powershell
# This command builds the 'PresetCreatorApp' target in Debug mode
cmake --build juce/build-ninja-debug --target PresetCreatorApp
```

-----
