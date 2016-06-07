set(SOURCES
        Vocab.cpp
        Vocab.h
        Types.h
        Corpus.cpp
        Corpus.h
        CorpusTypes.h
        tpt_typedefs.h
        MappedFile.cpp
        MappedFile.h
        TokenIndex.cpp
        TokenIndex.h
        qhashmap.hpp
)
add_library(src_objs OBJECT ${SOURCES})  # no real lib, just collection of objs
