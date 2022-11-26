#pragma once

#include <sc/shim/coro.hh>

namespace sc::io::test {

struct MockFuture {
public:
    costd::coroutine_handle<> m_handle;

    bool await_ready() { return false; }
    void await_suspend(costd::coroutine_handle<> handle) { m_handle = handle; }
    void await_resume() {}

    void resume() { m_handle.resume(); }
};

} // namespace sc::io::test
