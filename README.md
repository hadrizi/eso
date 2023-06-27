# eso
Simple ISO 9660 explorer. 
Allows to explore contents of Optical Disc Image without mounting it.

## Usage
```bash
usage: eso <isofile> <action>
<action>:
    list <folder>   - lists folder contents in the same manner as `ls` does
    cat <file>      - prints content of the file
```

## Build
```bash
gcc eso.c -o eso
```

## References
- https://wiki.osdev.org/ISO_9660 - OSDev Wiki ISO 9660 description
- https://www.iso.org/obp/ui/#iso:std:iso:9660:ed-1:v1:en - original ISO document
