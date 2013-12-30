#!/bin/bash

python ConvertJsonToBuf.py saves/example.json saves/example.buf

build/exampleGame saves/example.buf &