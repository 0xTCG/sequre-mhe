#!/usr/bin/env bash

echo "Cleaning up sockets ..."
find  . -name 'sock.*' -exec rm {} \;

echo "Copying DSL to Seq ..."

if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    export CP_OPTIONS=-ru
elif [[ "$OSTYPE" == "darwin"* ]]; then
    export CP_OPTIONS=-R
fi

cp $CP_OPTIONS dsl/* seq/stdlib/sequre/

echo "Compiling tests ..."
seq/build/seqc run -release client.seq test

echo "Cleaning up sockets ..."
find  . -name 'sock.*' -exec rm {} \;
