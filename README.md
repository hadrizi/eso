# eso
Simple ISO 9660 explorer

## Usage
```bash
usage: exo <isofile> <action>
<action>:
    list <folder>   - lists folder contents in the same manner as `ls` does
    cat <file>      - prints content of the file
```

## Build
```bash
gcc eso.c -o eso
```