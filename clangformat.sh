#!/bin/bash
find ./include -iname *.hpp -o -iname *.cpp -o -iname *.inl -o -iname *.hxx | xargs clang-format -i
find ./examples -maxdepth 1 -iname *.hpp -o -iname *.cpp -o -iname *.inl -o -iname *.hxx | xargs clang-format -i
find ./examples/lowlevel -iname *.hpp -o -iname *.cpp -o -iname *.inl -o -iname *.hxx | xargs clang-format -i
find ./test -iname *.hpp -o -iname *.cpp -o -iname *.inl -o -iname *.hxx | xargs clang-format -i
