#pragma once

#include <vector>

namespace seastar {

namespace container {

template<typename Container>
struct inserter {
    void size_hint(size_t);

    template<typename E>
    void insert(E&&);
};

template<typename T>
struct inserter<std::vector<T>> {
    std::vector<T>* vec;

    void size_hint(size_t sz) {
        vec->reserve(sz);
    }

    template<typename E>
    void insert(E&& e) {
        vec->push_back(std::forward<E>(e));
    }
};

template<typename Container>
inserter<Container> inserter_for(Container& c) {
    return inserter<Container>{&c};
}

}

}