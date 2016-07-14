# find_package(Boost) really belongs here.
# But then we should build a shared lib here.

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
        util/rbtree.hpp
        util/Time.h
        util/usage.cpp
        util/usage.h
        TokenIndexSpan.cpp
        TreeNode.cpp
        TreeNode.h
        DiskTreeNode.cpp DiskTreeNode.h DiskSuffixArray.cpp DiskSuffixArray.h)

add_library(sto OBJECT ${SOURCES})  # no real lib, just collection of objs
