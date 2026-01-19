# glTF Renderer
![preview image](Preview.png)

glTF Renderer is a DirectX 12 based renderer for glTF files. It implements both a path tracer and a traditional raster pipeline.

## Features

- Raster and path tracer renderers.
- Skeletal animation and morph targets.
- Environment mapping with support for .exr and .hdr formats.
- Tone mapping based on AgX.
- Drag and drop loading of glTF and environment maps.

## Supported glTF Extensions

- KHR_lights_punctual
- KHR_materials_anisotropy
- KHR_materials_clearcoat
- KHR_materials_emissive_strength
- KHR_materials_ior
- KHR_materials_sheen *
- KHR_materials_specular
- KHR_texture_transform

\* Image based lighting is not currently supported for sheen in the raster path.

## Requirements
A DirectX 12 capable graphics card with DXR tier 1.1 support is needed to run the path tracer.

## Getting Started
CMake is required to create the build files.
1. Clone the repository.
```
git clone https://github.com/l-johnson-code/glTF-Renderer.git
```
2. Get all submodules.
```
git submodule update --init --recursive
```
3. Generate build files.
```
cmake -B Build
```
4. Building directly with CMake (optional).
```
cmake --build Build
```
## Command line arguments
- `--height=[height]` Set window height.
- `--width=[width]` Set window width.
- `--fullscreen` Starts in a fullscreen borderless window.
- `--d3d12-debug-layer` Enable the DirectX 12 debug layer.
- `--gpu-based-validation` Enable DirectX 12 GPU based validation.
- `--environment-map=[filepath]` Loads the specified environment map on startup.
- `--gltf=[filepath]` Loads the specified glTF file on startup.

## Camera controls
The camera can be toggled between orbit and free mode in the camera settings.
### Orbit camera controls.
- Hold left click and drag to rotate the camera. 
- Hold right click and drag to pan the camera. 
- Use the mouse wheel to zoom in and out.
### Free camera controls
- Hold right click to enable camera control.
- Use WASD to move the camera.
- Use Q and E to move up and down. 
- Hold shift whilst moving to increase movement speed.
- Movement speed can be increased or decreased using the mousewheel.

## Pathtracer Settings
|Setting|Description|
|-|-|
|Maximum Bounces|Maximum possible bounces before a ray terminates.|
|Minimum Bounces|Minimum amount of bounces before Russian roulette can terminate rays.|
|Min Russian Roulette Probability|The minimum possibility a ray will continue when russian roulette is turned on.|
|Max Russian Roulette Probability|The maximum possibility a ray will continue when russian roulette is turned on.|
|Max ray length|Maximum ray length.|
|Debug Output|Alternate outputs for debugging assistance.|
|Use Frame As Seed|Use the frame counter as the seed for pseudo random number generation.|
|Seed|Specify seed for pseudo random number generation.|
|Enable Point Lights|Enables next event estimation sampling of point lights|
|Shadow Rays|Shadow rays will be traced for point lights.|
|Alpha Shadows|Alpha tested objects will be included in shadow rays. This gives improved shadows at the cost of performance.|
|Indirect Environment Only|Only primary camera rays will hit geometry. Any bounce rays will miss all geometry and sample the environment instead.|
|Cull Backface Triangles|Back facing triangles will be culled for single sided objects.|
|Accumulate|Rays will be accumulated.|
|Max Accumulated Frames|The maximum number of rays that will be accumulated. No additional rays will be traced after.|
|Enable Environment|Enable the environment map if one is loaded.|
|Importance Sample Environment|Trace an additional ray using an importance map and combine using multiple importance sampling. This can dramatically increase quality when using environment maps with small bright lights, at the cost of performance.|
|Enable Luminance Clamp|Rays with a high luminance will be clamped based on the their luminance.|
|Luminance Clamp|The luminance value at which bright rays will be clamped to.|
|Material Diffuse White|Render all objects using a white diffuse material.|
|Use Geometric Normal|Use the geometric normal for materials instead of vertex normals or normal mapping.|
|Use Multiple Importance Sampling|Use multiple importance sampling to sample the BSDF. If disabled, cosine weighted hemisphere sampling will be used instead.|
|Show NAN|Render NANs as red.|
|Show INF|Render INF as red.|

## Licence

This repository is licensed under the MIT license. See LICENSE.md for details.
