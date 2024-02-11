#pragma once

#include <mutex>
#include <vector>
#include <unordered_map>
#include "mvContext.h"

//-----------------------------------------------------------------------------
// mvFunctionWrapper
//-----------------------------------------------------------------------------

class mvFunctionWrapper
{
    struct impl_base {
        virtual void call() = 0;
        virtual ~impl_base() = default;
    };

    template<typename F>
    struct impl_type : impl_base
    {
        F f;
        explicit impl_type(F&& f) : f(std::move(f)) {}
        void call() override { f(); }
    };

public:

    mvFunctionWrapper() = default;

    template<typename F>
    mvFunctionWrapper(F&& f) : m_impl(new impl_type<F>(std::move(f))) {}

    mvFunctionWrapper(mvFunctionWrapper&& other) noexcept
            : m_impl(std::move(other.m_impl))
    {

    }

    mvFunctionWrapper& operator=(mvFunctionWrapper&& other)
    {
        m_impl = std::move(other.m_impl);
        return *this;
    }

    // delete copy constructor and assignment operator
    mvFunctionWrapper(const mvFunctionWrapper&) = delete;
    mvFunctionWrapper(mvFunctionWrapper&) = delete;
    mvFunctionWrapper& operator=(const mvFunctionWrapper&) = delete;

    void operator()()
    {
        m_impl->call();
    }

private:

    std::unique_ptr<impl_base> m_impl;

};

//-----------------------------------------------------------------------------
// mvCallbackWrapper
//-----------------------------------------------------------------------------

class mvCallbackWrapper
{
    PyObject* callback = nullptr;
    PyObject* userData = nullptr;

public:

    mvCallbackWrapper() = default;

    mvCallbackWrapper(PyObject* callback, PyObject* userData, bool borrow = true)
        : callback(callback), userData(userData)
    {
        if (borrow) {
            // If borrowing, we add our own reference.
            // If taking, we use the reference that the caller had.
            Py_XINCREF(callback);
            Py_XINCREF(userData);
        }
    }

    template<typename F>
    mvCallbackWrapper(F&& f) noexcept
    {
        callback = f.callback;
        userData = f.userData;
        f.callback = nullptr;
        f.userData = nullptr;
    }

    mvCallbackWrapper& operator=(mvCallbackWrapper&& other)
    {
        callback = other.callback;
        userData = other.userData;
        other.callback = nullptr;
        other.userData = nullptr;
        return *this;
    }

    void run(mvUUID sender = 0, PyObject *appData = nullptr, bool decrementAppData = true)
    {
        if (!callback) {
            return;
        }
        // Bump refs in case this object gets deleted before the callback's run!
        Py_XINCREF(callback);
        Py_XINCREF(userData);
        if (!decrementAppData)
            Py_XINCREF(appData);

        mvSubmitCallback([=]() {
            mvRunCallback(callback, sender, appData, userData);

            Py_XDECREF(callback);
            Py_XDECREF(userData);
            Py_XDECREF(appData);
            });
    }

    void run_blocking(mvUUID sender = 0, PyObject *appData = nullptr, bool decrementAppData = true)
    {
        if (!callback) {
            return;
        }

        mvRunCallback(callback, sender, appData, userData);
    }

    ~mvCallbackWrapper()
    {
        Py_XDECREF(callback);
        Py_XDECREF(userData);
    }

    // delete copy constructor and assignment operator
    mvCallbackWrapper(const mvCallbackWrapper&) = delete;
    mvCallbackWrapper(mvCallbackWrapper&) = delete;
    mvCallbackWrapper& operator=(const mvCallbackWrapper&) = delete;
};

//-----------------------------------------------------------------------------
// mvCallbackPythonSlot
//-----------------------------------------------------------------------------

class mvCallbackPythonSlot
{
    const char* pythonName;
    mvCallbackWrapper callbackWrapper;

public:
    mvCallbackPythonSlot(const char* pythonName)
        : pythonName(pythonName)
    {
    }

    PyObject* set_from_python(PyObject* self, PyObject* args, PyObject* kwargs);
    
    void run(mvUUID sender = 0, PyObject *appData = nullptr, bool decrementAppData = true)
    {
        callbackWrapper.run(sender, appData, decrementAppData);
    }

    void run_blocking(mvUUID sender = 0, PyObject *appData = nullptr, bool decrementAppData = true)
    {
        callbackWrapper.run_blocking(sender, appData, decrementAppData);
    }
};

//-----------------------------------------------------------------------------
// mvQueue
//-----------------------------------------------------------------------------
template<typename T>
class mvQueue
{

public:

    struct node
    {
        std::shared_ptr<T>    data;
        std::unique_ptr<node> next;
    };

public:

    mvQueue() : m_head(new node), m_tail(m_head.get()) {}

    // copy assignment and constructor deleted
    mvQueue(const mvQueue& other) = delete;
    mvQueue& operator=(const mvQueue& other) = delete;

    std::shared_ptr<T> wait_and_pop()
    {
        std::unique_ptr<node> const old_head = wait_pop_head();
        return old_head->data;
    }

    std::shared_ptr<T> try_pop()
    {
        std::unique_ptr<node> const old_head = try_pop_head();
        return old_head ? old_head->data : std::shared_ptr<T>();
    }

    void wait_and_pop(T& value)
    {
        std::unique_ptr<node> const old_head = wait_pop_head(value);
    }

    bool try_pop(T& value)
    {
        std::unique_ptr<node> const old_head = try_pop_head(value);
        if (old_head)
            return true;
        return false;
    }

    void push(T value)
    {
        std::shared_ptr<T> new_data = std::make_shared<T>(std::move(value));
        auto p = std::make_unique<node>();

        // scoped in order to unlock tail mutex before notifying other threads
        {
            std::lock_guard<std::mutex> tail_lock(m_tail_mutex);
            m_tail->data = std::move(new_data);
            node* const new_tail = p.get();
            m_tail->next = std::move(p);
            m_tail = new_tail;
        }

        m_data_cond.notify_one();
    }

    bool empty()
    {
        std::lock_guard<std::mutex> head_lock(m_head_mutex);
        return (m_head.get() == get_tail());
    }

private:

    node* get_tail()
    {
        std::lock_guard<std::mutex> tail_lock(m_tail_mutex);
        return m_tail;
    }

    std::unique_ptr<node> pop_head()
    {
        std::unique_ptr<node> old_head = std::move(m_head);
        m_head = std::move(old_head->next);
        return old_head;
    }

    std::unique_ptr<node> try_pop_head()
    {
        std::lock_guard<std::mutex> head_lock(m_head_mutex);
        if (m_head.get() == get_tail())
            return std::unique_ptr<node>();
        return pop_head();
    }

    std::unique_ptr<node> try_pop_head(T& value)
    {
        std::lock_guard<std::mutex> head_lock(m_head_mutex);
        if (m_head.get() == get_tail())
            return std::unique_ptr<node>();

        value = std::move(*m_head->data);
        return pop_head();
    }

    std::unique_ptr<node> wait_pop_head()
    {
        std::unique_lock<std::mutex> head_lock(wait_for_data());
        return pop_head();
    }

    std::unique_ptr<node> wait_pop_head(T& value)
    {
        std::unique_lock<std::mutex> head_lock(wait_for_data());
        value = std::move(*m_head->data);
        return pop_head();
    }

    std::unique_lock<std::mutex> wait_for_data()
    {
        std::unique_lock<std::mutex> head_lock(m_head_mutex);
        m_data_cond.wait(head_lock, [&] {return m_head.get() != get_tail(); });
        return head_lock;
    }

private:

    std::mutex              m_head_mutex;
    std::mutex              m_tail_mutex;
    std::unique_ptr<node>   m_head;
    node* m_tail;
    std::condition_variable m_data_cond;

};

static PyObject* SanitizeCallback(PyObject* callback)
{
	if (callback == Py_None)
		return nullptr;

	return callback;
}

struct mvCallbackJob
{
	mvUUID      sender    = 0;
	PyObject*   callback  = nullptr;
	PyObject*   app_data  = nullptr;
	PyObject*   user_data = nullptr;
	std::string sender_str;
};

struct mvCallbackRegistry
{
	const i32 maxNumberOfCalls = 50;

	std::vector<mvCallbackJob> jobs;
	mvQueue<mvFunctionWrapper> tasks;
	mvQueue<mvFunctionWrapper> calls;
	std::atomic<b8>            running = false;
	std::atomic<i32>           callCount = 0;

	// callbacks
    mvCallbackPythonSlot viewportResizeCallbackSlot { "set_viewport_resize_callback" };
    mvCallbackPythonSlot exitCallbackSlot { "set_exit_callback" };
    mvCallbackPythonSlot dragEnterCallbackSlot { "set_drag_enter_callback" };
    mvCallbackPythonSlot dragLeaveCallbackSlot { "set_drag_leave_callback" };
    mvCallbackPythonSlot dragOverCallbackSlot { "set_drag_over_callback" };
    mvCallbackPythonSlot dropCallbackSlot { "set_drop_callback" };

	i32 highestFrame = 0;
	std::unordered_map<i32, PyObject*> frameCallbacks;
	std::unordered_map<i32, PyObject*> frameCallbacksUserData;
};

void mvFrameCallback(i32 frame);
void mvRunTasks();
template <typename SENDER>
void mvRunCallback(PyObject* callback, SENDER sender, PyObject* app_data, PyObject* user_data);
template <typename SENDER>
void mvAddCallback(PyObject* callback, SENDER sender, PyObject* app_data, PyObject* user_data);
bool mvRunCallbacks();

template<typename F, typename ...Args>
std::future<typename std::invoke_result<F, Args...>::type> mvSubmitTask(F f)
{

	typedef typename std::invoke_result<F, Args...>::type result_type;
	std::packaged_task<result_type()> task(std::move(f));
	std::future<result_type> res(task.get_future());

	if (GContext->started)
		GContext->callbackRegistry->tasks.push(std::move(task));
	else
		task();

	return res;
}

template<typename F, typename ...Args>
std::future<typename std::invoke_result<F, Args...>::type> mvSubmitCallback(F f)
{

	if (GContext->callbackRegistry->callCount > GContext->callbackRegistry->maxNumberOfCalls)
	{
		return {};
	}

	GContext->callbackRegistry->callCount++;

	typedef typename std::invoke_result<F, Args...>::type result_type;
	std::packaged_task<result_type()> task(std::move(f));
	std::future<result_type> res(task.get_future());

	GContext->callbackRegistry->calls.push(std::move(task));

	return res;
}