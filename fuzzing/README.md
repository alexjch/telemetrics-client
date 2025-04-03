# Fuzzing

```
    # Compile
    clang -g -fsanitize=fuzzer -I ${PWD}/src -I ${PWD} \
        -DFUZZING=1 -DABSTOPSRCDIR="" -DDATADIR="" -DBACKEND_ADDR=\""localhost\"" \
        -DLOCALSTATEDIR="\"/tmp\"" -I ${PWD}/src/nica -ltelemetry \
        -o fuzzing/fuzzing  src/common.c  src/telemdaemon.c  \
        src/nica/hashmap.c src/nica/inifile.c src/util.c \
        src/configuration.c fuzzing/fuzzing.c
    # Run
    ./run-fuzz.sh
```