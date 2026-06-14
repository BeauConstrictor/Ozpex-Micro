# Architecture

This document covers the memory map, device details and
design motivations in depth. If you just want to get started with the
Ozpex Micro, see the [README](/README.md).

The Ozpex Micro is unique primarily because of its device model, which
is covered extensively later in this documentation. To understand the
devices, you must first understand the memory map:

RAM:      `0x0000` -> `0xbfff` (inclusive), 48K
DEVICES:  `0xc000` -> `0xc002` (inclusive), 259B
BIOS ROM: `0xe000` -> `0xffff` (inclusive), 8k
