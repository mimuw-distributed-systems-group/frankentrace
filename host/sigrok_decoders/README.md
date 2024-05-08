# Protocol decoders for Sigrok

To make libsigrokdecoder-based utils (sigrok-cli, PulseView, etc.) see those decoders, make a symlink:

This is a slightly patched `arm_itm` decoder to support easier processing.

```shell
mkdir -p $HOME/.local/share/libsigrokdecode
ln -s $HOME/.local/share/libsigrokdecode/decoders $PWD
```
