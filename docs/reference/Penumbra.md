# Penumbra Reference

[Penumbra](https://github.com/reeltwo/Penumbra) code is located in build/Penumbra folder, if not present clone the repository to that location.

## Summary

### Pros

- Builds on top of Reeltwo repository.
- Web Interface for modifying parameters in real-time.
- Identification of Bluetooth Controller MAC Address to limit scanning and connection attempts from Bluetooth Controllers we don't want to interfere with the program.
- Separating out user settings allows for easy modification of some parameters without having to modify the core source code.
- Can handle Over-The-Air (OTA) update of firmware.

### Cons

- Inherits all Pros/Cons from Reeltwo repository.
- Organization of source code dumps everything into early Arduino style (i.e. monolithic .ino file), making it difficult to navigate and debug.
- Builds upon the PSController class, which tightly couples Bluetooth Controller input with program execution, making it difficult to swap the Bluetooth Controller for a different type; or change the input mapping to suit a different controller.

## Notable Files

- src/Fixed_Settings.h
- src/Variable_Settings.h
- pin-map.h
- User_Settings.h