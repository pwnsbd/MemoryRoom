# Meadow and 360 viewing

- Retains the original tree assets; removes all 100 experimental cone-tree instances. Adds 36 original-style pines beyond the existing 27, with 2,500 grass clumps outside the central photo area.
- Three low, muted distant mountain silhouettes. Lightweight height fog begins 15 m away, with volumetric fog disabled. Volumetric clouds are excluded from the game.
- Full monoscopic 2:1 equirectangular photos are labelled 360. Point and press trigger to enter; press trigger or grip again to return. Grip retains ordinary flat-photo handling.
- Panorama viewer uses one unlit textured sphere, tracks camera translation, and restores scene visibility on exit. Expanded panorama textures are capped at 4,096 pixels on desktop and Android. Android uses its existing subsampled image decoder.
- Detection uses approximately 2:1 dimensions; cropped panoramas and stereo layouts are not supported. A normal 2:1 photograph can also be classified as 360.

Editor Win64 Development build succeeded. A real panorama from D:/img/jpg rendered upright in simulation. Standalone APK packaging, controller use and sustained Quest performance must still be tested on the headset.

Backup of the previous map and changed source files: Z:/Applications/MemoryRoom-art360/original.
Final tree/mountain adjustment: final_scene.py (do not rerun the older author_art.py, which describes the superseded cone-tree style).

Final regression: panorama texture binding, opening, closing, completed collapse animation and fog/scene visibility restoration passed in PIE using a 64x32 PNG fixture. The D:/img/jpg directory was unavailable in the final session; its project setting is unchanged.
