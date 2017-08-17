/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef ERIZO_SRC_ERIZO_PIPELINE_PIPELINE_H_
#define ERIZO_SRC_ERIZO_PIPELINE_PIPELINE_H_

#include <string>
#include <vector>

#include "pipeline/HandlerContext.h"
#include "pipeline/ServiceContext.h"
#include "MediaDefinitions.h"

namespace erizo {

class PipelineBase;
class WebRtcConnection;

class PipelineManager {
 public:
  virtual ~PipelineManager() = default;
  virtual void deletePipeline(PipelineBase* pipeline) = 0;
  virtual void refreshTimeout() {}
};

class PipelineBase : public std::enable_shared_from_this<PipelineBase> {
 public:
  virtual ~PipelineBase() = default;

  void setPipelineManager(PipelineManager* manager) {
    manager_ = manager;
  }

  PipelineManager* getPipelineManager() {
    return manager_;
  }

  void deletePipeline() {
    if (manager_) {
      manager_->deletePipeline(this);
    }
  }

  template <class H>
  PipelineBase& addBack(std::shared_ptr<H> handler);

  template <class H>
  PipelineBase& addBack(H&& handler);

  template <class H>
  PipelineBase& addBack(H* handler);

  template <class H>
  PipelineBase& addFront(std::shared_ptr<H> handler);

  template <class H>
  PipelineBase& addFront(H&& handler);

  template <class H>
  PipelineBase& addFront(H* handler);

  // remove a Handler
  template <class H>
  PipelineBase& remove(H* handler);

  // remove all Handler which type is H
  template <class H>
  PipelineBase& remove();

  PipelineBase& removeFront();

  PipelineBase& removeBack();

  // getContext(i)->getHandler();
  template <class H>
  H* getHandler(int i);

  /*
  return first Handler of type H
  getContext<H>()->getHandler();
  */
  template <class H>
  H* getHandler();

  template <class H>
  typename ContextType<H>::type* getContext(int i);

  // 利用dynamic_cast, 返回第一个包含Handler类型为H的Context
  template <class H>
  typename ContextType<H>::type* getContext();

  template <class S>
  void addService(std::shared_ptr<S> service);

  template <class S>
  void addService(S&& service);

  template <class S>
  void addService(S* service);

  template <class S>
  void removeService();

  template <class S>
  typename ServiceContextType<S>::type* getServiceContext();

  template <class S>
  std::shared_ptr<S> getService();

  // If one of the handlers owns the pipeline itself, use setOwner to ensure
  // that the pipeline doesn't try to detach the handler during destruction,
  // lest destruction ordering issues occur.
  // See thrift/lib/cpp2/async/Cpp2Channel.cpp for an example
  template <class H>
  bool setOwner(H* handler);

  virtual void finalize() = 0;

 protected:
  template <class Context>
  void addContextFront(Context* ctx);

  void detachHandlers();
  /*
  - Context是内部上下文衔接类， 有in,out和Both三个方向类型，主要有 nextIn，nextOut 和 Handler 成员, Context的
    - fireXXX (read) -> nextIn
    - fireXXX (write) -> nextOut
    - XXX (read/write) -> Handler
  - Handler的 xxx 方法默认实现调用本 context 的 fireXXX，将数据传送到 下一个Context
  - Handler子类必须重载XXX方法加入自己逻辑，处理完毕后根据传递需要来调用父类方法.
  - Pipeline根据方向管理这两条Context链表，对外提供 Handler 和 Service 类
  */
  std::vector<std::shared_ptr<PipelineContext>> ctxs_; ///< 管理生存期
  std::vector<PipelineContext*> inCtxs_; ///< in或Both链表
  std::vector<PipelineContext*> outCtxs_;///< out或Both链表

  std::vector<std::shared_ptr<PipelineServiceContext>> service_ctxs_;

 private:
  PipelineManager* manager_{nullptr};

  template <class Context>
  PipelineBase& addHelper(std::shared_ptr<Context>&& ctx, bool front);  // NOLINT

  template <class H>
  PipelineBase& removeHelper(H* handler, bool checkEqual);

  typedef std::vector<std::shared_ptr<PipelineContext>>::iterator
    ContextIterator;

  ContextIterator removeAt(const ContextIterator& it);

  std::shared_ptr<PipelineContext> owner_;
};

/*
 * R is the inbound type, i.e. inbound calls start with pipeline.read(R)
 * W is the outbound type, i.e. outbound calls start with pipeline.write(W)
 *
 * Use Unit for one of the types if your pipeline is unidirectional.
 * If R is Unit, read(), readEOF(), and readException() will be disabled.
 * If W is Unit, write() and close() will be disabled.
 */
class Pipeline : public PipelineBase {
 public:
  using Ptr = std::shared_ptr<Pipeline>;

  static Ptr create() {
    return std::shared_ptr<Pipeline>(new Pipeline());
  }

  ~Pipeline();

  void read(packetPtr packet);
  void readEOF();
  void transportActive();
  void transportInactive();

  void write(packetPtr packet);
  void close();

  // 将所有in，out以头尾链表方式链起来，并触发attachPipeline回调
  void finalize() override;

  void notifyUpdate();
  void enable(std::string name);
  void disable(std::string name);

 protected:
  Pipeline();

 private:
  InboundLink* front_{nullptr};
  OutboundLink* back_{nullptr};
};

}  // namespace erizo

#include <pipeline/Pipeline-inl.h>

#endif  // ERIZO_SRC_ERIZO_PIPELINE_PIPELINE_H_
