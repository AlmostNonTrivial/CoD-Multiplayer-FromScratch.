
## Prerequisites

Install the required dependencies for your platform:

**Linux (Ubuntu/Debian):**

bash

```bash
sudo apt-get install cmake libglfw3-dev libfreetype-dev libgl1-mesa-dev
```

**macOS:**

bash

```bash
brew install cmake glfw freetype
```

**Windows:**

-   Install [CMake](https://cmake.org/download/)
-   Download [GLFW](https://www.glfw.org/download.html) and install to `C:\Program Files\GLFW`
-   Install FreeType (via vcpkg or manual download)

## Building

bash

```bash
# Create build directory
mkdir build
cd build

# Configure (Release build)
cmake ..

# Or configure for Debug build
cmake -DCMAKE_BUILD_TYPE=Debug ..

# Build
cmake --build .
```

## Running

bash

```bash
# From build directory
./COD        # Linux/macOS
COD.exe      # Windows
```

## Project Structure

-   `src/` - Application source files (.cpp)
-   `lib/` - Third-party library source files (.c)

## Build Configuration

-   **C++ Standard:** C++17
-   **Debug flags:** `-g -O0 -DDEBUG` (GCC/Clang) or `/Zi /Od /DDEBUG` (MSVC)
-   **Warnings:** Enabled with `-Wall -Wextra` (unused parameter/variable warnings suppressed)
