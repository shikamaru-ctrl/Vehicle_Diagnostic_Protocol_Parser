# VDPFrameParser Project

This project contains the implementation of a VDP (Vehicle Diagnostic Protocol) frame parser, along with supporting documentation, tests, and a sample application.

## Project Structure

- `VDPFrameParser/`: Contains the source code for the VDP parser library.
  - `include/`: Header files for the parser (`vdp_parser.h`) and type definitions.
  - `src/`: Implementation files for the parser (`vdp_parser.cpp`).
  - `test/`: Unit tests for the parser.
- `main.cpp`: A simple command-line application to test the VDP parser library using sample frame data.
- `sample_frames.hex` / `sample_frames_corrected.hex`: Sample hex data used for testing the parser.

## Documentation Files

This directory contains several key documents that explain the architecture, design decisions, and history of the project.

### `ARCHITECTURE.md`
This file provides a comprehensive overview of the VDPFrameParser's architecture. It includes details on the different layers of the system, performance considerations, and profiling information. This is the primary document for understanding the technical design.

### `initial_archdiagram.png`
This image file contains the initial architecture diagram that was created for the project's approval phase. It offers a high-level visual representation of the original design concept.
