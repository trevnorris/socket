#ifndef APPLE_SSC_H
#define APPLE_SSC_H

#include "include/uv.h"
#include "common.hh"

using Tasks = std::map<std::string, id<WKURLSchemeTask>>;
using PostRequests = std::map<uint64_t, NSData*>;

//
// Common IO and bridge impl for iOS and MacOS
//
@interface SocketCore : NSObject

@property nw_path_monitor_t monitor;
@property (strong, nonatomic) NSObject<OS_dispatch_queue>* monitorQueue;
@property (strong, nonatomic) WKWebView* webview;
@property (strong, nonatomic) WKUserContentController* content;

- (id<WKURLSchemeTask>) getTask: (std::string)id;
- (void) removeTask: (std::string)id;
- (NSData*) getPost: (uint64_t)id;
- (void) removePost: (uint64_t)id;

// Bridge
- (void) emit: (std::string)event message: (std::string)message;
- (void) emit: (std::string)event params: (std::string)params buf: (char*)buf;
- (void) resolve: (std::string)seq message: (std::string)message;
- (void) reject: (std::string)seq message: (std::string)message;

// Filesystem
- (void) fsOpen: (std::string)seq id: (uint64_t)id path: (std::string)path flags: (int)flags;
- (void) fsClose: (std::string)seq id: (uint64_t)id;
- (void) fsRead: (std::string)seq id: (uint64_t)id len: (int)len offset: (int)offset;
- (void) fsWrite: (std::string)seq id: (uint64_t)id data: (std::string)data offset: (int64_t)offset;
- (void) fsStat: (std::string)seq path: (std::string)path;
- (void) fsUnlink: (std::string)seq path: (std::string)path;
- (void) fsRename: (std::string)seq pathA: (std::string)pathA pathB: (std::string)pathB;
- (void) fsCopyFile: (std::string)seq pathA: (std::string)pathA pathB: (std::string)pathB flags: (int)flags;
- (void) fsRmDir: (std::string)seq path: (std::string)path;
- (void) fsMkDir: (std::string)seq path: (std::string)path mode: (int)mode;
- (void) fsReadDir: (std::string)seq path: (std::string)path;

// TCP
- (void) tcpBind: (std::string)seq serverId: (uint64_t)serverId ip: (std::string)ip port: (int)port;
- (void) tcpConnect: (std::string)seq clientId: (uint64_t)clientId port: (int)port ip: (std::string)ip;
- (void) tcpSetTimeout: (std::string)seq clientId: (uint64_t)clientId timeout: (int)timeout;
- (void) tcpSetKeepAlive: (std::string)seq clientId: (uint64_t)clientId timeout: (int)timeout;
- (void) tcpSend: (uint64_t)clientId message: (std::string)message;
- (void) tcpReadStart: (std::string)seq clientId: (uint64_t)clientId;

// UDP
- (void) udpBind: (std::string)seq serverId: (uint64_t)serverId ip: (std::string)ip port: (int)port;
- (void) udpSend: (std::string)seq clientId: (uint64_t)clientId message: (std::string)message offset: (int)offset len: (int)len port: (int)port ip: (const char*)ip;
- (void) udpReadStart: (std::string)seq serverId: (uint64_t)serverId;

// Common
- (void) sendBufferSize: (std::string)seq clientId: (uint64_t)clientId size: (int)size;
- (void) recvBufferSize: (std::string)seq clientId: (uint64_t)clientId size: (int)size;
- (void) close: (std::string)seq clientId: (uint64_t)clientId;
- (void) shutdown: (std::string)seq clientId: (uint64_t)clientId;
- (void) readStop: (std::string)seq clientId: (uint64_t)clientId;

// Network
- (void) dnsLookup: (std::string)seq hostname: (std::string)hostname;
- (void) initNetworkStatusObserver;
- (std::string) getNetworkInterfaces;
@end

dispatch_queue_attr_t qos = dispatch_queue_attr_make_with_qos_class(
  DISPATCH_QUEUE_CONCURRENT,
  QOS_CLASS_USER_INITIATED,
  -1
);

static dispatch_queue_t queue = dispatch_queue_create("ssc.queue", qos);

struct GenericContext {
  AppDelegate* delegate;
  uint64_t id;
  std::string seq;
};

struct DescriptorContext {
  uv_file fd;
  std::string seq;
  AppDelegate* delegate;
  uint64_t id;
};

struct DirectoryReader {
  uv_dirent_t dirents;
  uv_dir_t* dir;
  uv_fs_t reqOpendir;
  uv_fs_t reqReaddir;
  AppDelegate* delegate;
  std::string seq;
};

struct Peer {
  AppDelegate* delegate;
  std::string seq;

  uv_tcp_t* tcp;
  uv_udp_t* udp;
  uv_stream_t* stream;

  ~Peer () {
    delete this->tcp;
    delete this->udp;
    delete this->udx;
  };
};

struct Server : public Peer {
  uint64_t serverId;
};

struct Client : public Peer {
  Server* server;
  uint64_t clientId;
};

std::string addrToIPv4 (struct sockaddr_in* sin) {
  char buf[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &sin->sin_addr, buf, INET_ADDRSTRLEN);
  return std::string(buf);
}

std::string addrToIPv6 (struct sockaddr_in6* sin) {
  char buf[INET6_ADDRSTRLEN];
  inet_ntop(AF_INET6, &sin->sin6_addr, buf, INET6_ADDRSTRLEN);
  return std::string(buf);
}

struct PeerInfo {
  std::string ip = "";
  std::string family = "";
  int port = 0;
  int error = 0;
  void init(uv_tcp_t* connection);
  void init(uv_udp_t* socket);
};

void PeerInfo::init (uv_tcp_t* connection) {
  int namelen;
  struct sockaddr_storage addr;
  namelen = sizeof(addr);

  error = uv_tcp_getpeername(connection, (struct sockaddr*) &addr, &namelen);

  if (error) {
    return;
  }

  if (addr.ss_family == AF_INET) {
    family = "ipv4";
    ip = addrToIPv4((struct sockaddr_in*) &addr);
    port = (int) htons(((struct sockaddr_in*) &addr)->sin_port);
  } else {
    family = "ipv6";
    ip = addrToIPv6((struct sockaddr_in6*) &addr);
    port = (int) htons(((struct sockaddr_in6*) &addr)->sin6_port);
  }
}

void PeerInfo::init (uv_udp_t* socket) {
  int namelen;
  struct sockaddr_storage addr;
  namelen = sizeof(addr);

  error = uv_udp_getpeername(socket, (struct sockaddr*) &addr, &namelen);

  if (error) {
    return;
  }

  if (addr.ss_family == AF_INET) {
    family = "ipv4";
    ip = addrToIPv4((struct sockaddr_in*) &addr);
    port = (int) htons(((struct sockaddr_in*) &addr)->sin_port);
  } else {
    family = "ipv6";
    ip = addrToIPv6((struct sockaddr_in6*) &addr);
    port = (int) htons(((struct sockaddr_in6*) &addr)->sin6_port);
  }
}

static void parseAddress (struct sockaddr *name, int* port, char* ip) {
  struct sockaddr_in *name_in = (struct sockaddr_in *) name;
  *port = ntohs(name_in->sin_port);
  uv_ip4_name(name_in, ip, 17);
}

std::map<uint64_t, Client*> clients;
std::map<uint64_t, Server*> servers;
std::map<uint64_t, GenericContext*> contexts;
std::map<uint64_t, DescriptorContext*> descriptors;

using UDXSendRequest = udx_socket_send_t;
using UDXWriteRequest = udx_stream_write_t;

struct sockaddr_in addr;

typedef struct {
  uv_write_t req;
  uv_buf_t buf;
} write_req_t;

uv_loop_t *loop = uv_default_loop();

void loopCheck () {
  if (uv_loop_alive(loop) == 0) {
    uv_run(loop, UV_RUN_DEFAULT);
  }
}

@implementation SocketCore
Tasks tasks;
PostRequests posts;

- (id<WKURLSchemeTask>) getTask: (std::string)id {
  if (posts.find(id) == posts.end()) return nullptr;
  return tasks.at(id);
}

- (void) removeTask: (std::string)id {
  if (tasks.find(id) == tasks.end()) return;
  tasks.erase(id);
}

- (NSData*) getPost: (uint64_t)id {
  if (posts.find(id) == posts.end()) return nullptr;
  return posts.at(id);
}

- (void) removePost: (uint64_t)id {
  if (posts.find(id) == posts.end()) return;
  posts.erase(id);
}

- (void) emit: (std::string)event message: (std::string)message {
  NSString* script = [NSString stringWithUTF8String: SSC::emitToRenderProcess(event, SSC::encodeURIComponent(message)).c_str()];
  [self.webview evaluateJavaScript: script completionHandler:nil];
}

- (void) emit: (std::string)event params: (std::string)params buf: (char*)buf {
  uint64_t id = SSC::rand64();

  std::string sid = std::to_string(id);

  std::string js(
    "const xhx = new XMLHttpRequest();"
    "xhr.open('ipc://post?id=" + sid + "');"
    "xhr.onload = e => {"
    "  const o = new URLSearchParams('" + params + "');"
    "  const detail = {"
    "    data: xhr.response," +
    "    params: Object.fromEntries(o)"
    "  };"
    "  window._ipc.emit('" + event + "', detail);"
    "}"
  );

  NSString* str = [NSString stringWithUTF8String:buf];
  NSData* data = [str dataUsingEncoding: NSUTF8StringEncoding];
  self.posts.insert_or_assign(id, data);

  NSString* script = [NSString stringWithUTF8String: js.c_str()];
  [self.webview evaluateJavaScript: script completionHandler: nil];
}

- (void) resolve: (std::string)seq message: (std::string)message {
  if (self.tasks->find(seq) != self.tasks->end()) {
    auto task = self.tasks.at(seq);

    NSHTTPURLResponse *httpResponse = [[NSHTTPURLResponse alloc]
      initWithURL: task.request.URL
       statusCode: 200
      HTTPVersion: @"HTTP/1.1"
     headerFields: nil
    ];

    [task didReceiveResponse: httpResponse];

    NSString* str = [NSString stringWithUTF8String:message.c_str()];
    NSData* data = [str dataUsingEncoding: NSUTF8StringEncoding];

    [task didReceiveData: data];
    [task didFinish];

    self.tasks->erase(seq);
    return;
  }

  NSString* script = [NSString stringWithUTF8String: SSC::resolveToRenderProcess(seq, "0", SSC::encodeURIComponent(message)).c_str()];
  [self.webview evaluateJavaScript: script completionHandler:nil];
}

//
// Filesystem Methods
//
- (void) fsOpen: (std::string)seq id: (uint64_t)id path: (std::string)path flags: (int)flags {
  dispatch_async(queue, ^{
    auto desc = descriptors[id] = new DescriptorContext;
    desc->id = id;
    desc->delegate = self;
    desc->seq = seq;

    uv_fs_t req;
    req.data = desc;

    int fd = uv_fs_open(loop, &req, (char*) path.c_str(), flags, 0, [](uv_fs_t* req) {
      auto desc = static_cast<DescriptorContext*>(req->data);

      dispatch_async(dispatch_get_main_queue(), ^{
        [desc->delegate resolve: desc->seq message: SSC::format(R"JSON({
          "data": {
            "fd": $S
          }
        })JSON", std::to_string(desc->id))];

        uv_fs_req_cleanup(req);
      });
    });

    if (fd < 0) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self resolve: seq message: SSC::format(R"({
          "err": {
            "id": "$S",
            "message": "$S"
          }
        })", std::to_string(id), std::string(uv_strerror(fd)))];
      });
      return;
    }

    desc->fd = fd;
    loopCheck();
  });
};

- (void) fsClose: (std::string)seq id: (uint64_t)id {
  dispatch_async(queue, ^{
    auto desc = descriptors[id];
    desc->seq = seq;

    if (desc == nullptr) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self resolve: seq message: SSC::format(R"JSON({
          "err": {
            "code": "ENOTOPEN",
            "message": "No file descriptor found with that id"
          }
        })JSON")];
      });
      return;
    }

    uv_fs_t req;
    req.data = desc;

    int err = uv_fs_close(loop, &req, desc->fd, [](uv_fs_t* req) {
      auto desc = static_cast<DescriptorContext*>(req->data);

      dispatch_async(dispatch_get_main_queue(), ^{
        [desc->delegate resolve: desc->seq message: SSC::format(R"JSON({
          "data": {
            "fd": $S
          }
        })JSON", std::to_string(desc->id))];

        uv_fs_req_cleanup(req);
      });
    });

    if (err) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self resolve: seq message: SSC::format(R"({
          "err": {
            "id": "$S",
            "message": "$S"
          }
        })", std::to_string(id), std::string(uv_strerror(err)))];
      });
    }

    loopCheck();
  });
};

- (void) fsRead: (std::string)seq id: (uint64_t)id len: (int)len offset: (int)offset {
  dispatch_async(queue, ^{
    auto desc = descriptors[id];

    if (desc == nullptr) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self resolve: seq message: SSC::format(R"JSON({
          "err": {
            "code": "ENOTOPEN",
            "message": "No file descriptor found with that id"
          }
        })JSON")];
      });
      return;
    }

    desc->delegate = self;
    desc->seq = seq;

    uv_fs_t req;
    req.data = desc;

    auto buf = new char[len];
    const uv_buf_t iov = uv_buf_init(buf, (int) len);

    int err = uv_fs_read(loop, &req, desc->fd, &iov, 1, offset, [](uv_fs_t* req) {
      auto desc = static_cast<DescriptorContext*>(req->data);

      if (req->result < 0) {
        dispatch_async(dispatch_get_main_queue(), ^{
          [desc->delegate resolve: desc->seq message: SSC::format(R"JSON({
            "err": {
              "code": "ENOTOPEN",
              "message": "No file descriptor found with that id"
            }
          })JSON")];
        });
        return;
      }

      char *data = req->bufs[0].base;

      NSString* str = [NSString stringWithUTF8String: data];
      NSData *nsdata = [str dataUsingEncoding:NSUTF8StringEncoding];
      NSString *base64Encoded = [nsdata base64EncodedStringWithOptions:0];

      auto message = std::string([base64Encoded UTF8String]);

      dispatch_async(dispatch_get_main_queue(), ^{
        [desc->delegate resolve: desc->seq message: SSC::format(R"({
          "id": "$S",
          "result": "$i",
          "data": "$S"
        })", std::to_string(desc->id), (int)req->result, message)];

        uv_fs_req_cleanup(req);
      });
    });

    if (err) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self resolve: seq message: SSC::format(R"({
          "err": {
            "id": "$S",
            "message": "$S"
          }
        })", std::to_string(id), std::string(uv_strerror(err)))];
      });
    }

    loopCheck();
  });
};

- (void) fsWrite: (std::string)seq id: (uint64_t)id data: (std::string)data offset: (int64_t)offset {
  dispatch_async(queue, ^{
    auto desc = descriptors[id];

    if (desc == nullptr) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self resolve: seq message: SSC::format(R"JSON({
          "err": {
            "code": "ENOTOPEN",
            "message": "No file descriptor found with that id"
          }
        })JSON")];
      });
      return;
    }

    desc->delegate = self;
    desc->seq = seq;

    uv_fs_t req;
    req.data = desc;

    const uv_buf_t buf = uv_buf_init((char*) data.c_str(), (int) data.size());

    int err = uv_fs_write(uv_default_loop(), &req, desc->fd, &buf, 1, offset, [](uv_fs_t* req) {
      auto desc = static_cast<DescriptorContext*>(req->data);

      dispatch_async(dispatch_get_main_queue(), ^{
        [desc->delegate resolve: desc->seq message: SSC::format(R"({
          "id": "$S",
          "result": "$i"
        })", std::to_string(desc->id), (int)req->result)];

        uv_fs_req_cleanup(req);
      });
    });

    if (err) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self resolve: seq message: SSC::format(R"({
          "err": {
            "id": "$S",
            "message": "$S"
          }
        })", std::to_string(id), std::string(uv_strerror(err)))];
      });
    }

    loopCheck();
  });
};

- (void) fsStat: (std::string)seq path: (std::string)path {
  dispatch_async(queue, ^{
    uv_fs_t req;
    DescriptorContext* desc = new DescriptorContext;
    desc->seq = seq;
    desc->delegate = self;
    req.data = desc;

    int err = uv_fs_stat(loop, &req, (const char*) path.c_str(), [](uv_fs_t* req) {
      auto desc = static_cast<DescriptorContext*>(req->data);

      dispatch_async(dispatch_get_main_queue(), ^{
        [desc->delegate resolve: desc->seq message: SSC::format(R"({
          "id": "$S",
          "result": "$i"
        })", std::to_string(desc->id), (int)req->result)];

        delete desc;
        uv_fs_req_cleanup(req);
      });
    });

    if (err) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self resolve: seq message: SSC::format(R"({
          "err": {
            "message": "$S"
          }
        })", std::string(uv_strerror(err)))];
      });
    }

    loopCheck();
  });
};

- (void) fsUnlink: (std::string)seq path: (std::string)path {
  dispatch_async(queue, ^{
    uv_fs_t req;
    DescriptorContext* desc = new DescriptorContext;
    desc->seq = seq;
    desc->delegate = self;
    req.data = desc;

    int err = uv_fs_unlink(loop, &req, (const char*) path.c_str(), [](uv_fs_t* req) {
      auto desc = static_cast<DescriptorContext*>(req->data);

      dispatch_async(dispatch_get_main_queue(), ^{
        [desc->delegate resolve: desc->seq message: SSC::format(R"({
          "id": "$S",
          "result": "$i"
        })", std::to_string(desc->id), (int)req->result)];

        delete desc;
        uv_fs_req_cleanup(req);
      });
    });

    if (err) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self resolve: seq message: SSC::format(R"({
          "err": {
            "message": "$S"
          }
        })", std::string(uv_strerror(err)))];
      });
    }
    loopCheck();
  });
};

- (void) fsRename: (std::string)seq pathA: (std::string)pathA pathB: (std::string)pathB {
  dispatch_async(queue, ^{
    uv_fs_t req;
    DescriptorContext* desc = new DescriptorContext;
    desc->seq = seq;
    desc->delegate = self;
    req.data = desc;

    int err = uv_fs_rename(loop, &req, (const char*) pathA.c_str(), (const char*) pathB.c_str(), [](uv_fs_t* req) {
      auto desc = static_cast<DescriptorContext*>(req->data);

      dispatch_async(dispatch_get_main_queue(), ^{
        [desc->delegate resolve: desc->seq message: SSC::format(R"({
          "id": "$S",
          "result": "$i"
        })", std::to_string(desc->id), (int)req->result)];

        delete desc;
        uv_fs_req_cleanup(req);
      });
    });

    if (err) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self resolve: seq message: SSC::format(R"({
          "err": {
            "message": "$S"
          }
        })", std::string(uv_strerror(err)))];
      });
    }
    loopCheck();
  });
};

- (void) fsCopyFile: (std::string)seq pathA: (std::string)pathA pathB: (std::string)pathB flags: (int)flags {
  dispatch_async(queue, ^{
    uv_fs_t req;
    DescriptorContext* desc = new DescriptorContext;
    desc->seq = seq;
    desc->delegate = self;
    req.data = desc;

    int err = uv_fs_copyfile(loop, &req, (const char*) pathA.c_str(), (const char*) pathB.c_str(), flags, [](uv_fs_t* req) {
      auto desc = static_cast<DescriptorContext*>(req->data);

      dispatch_async(dispatch_get_main_queue(), ^{
        [desc->delegate resolve: desc->seq message: SSC::format(R"({
          "id": "$S",
          "result": "$i"
        })", std::to_string(desc->id), (int)req->result)];

        delete desc;
        uv_fs_req_cleanup(req);
      });
    });

    if (err) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self resolve: seq message: SSC::format(R"({
          "err": {
            "message": "$S"
          }
        })", std::string(uv_strerror(err)))];
      });
    }
    loopCheck();
  });
};

- (void) fsRmDir: (std::string)seq path: (std::string)path {
  dispatch_async(queue, ^{
    uv_fs_t req;
    DescriptorContext* desc = new DescriptorContext;
    desc->seq = seq;
    desc->delegate = self;
    req.data = desc;

    int err = uv_fs_rmdir(loop, &req, (const char*) path.c_str(), [](uv_fs_t* req) {
      auto desc = static_cast<DescriptorContext*>(req->data);

      dispatch_async(dispatch_get_main_queue(), ^{
        [desc->delegate resolve: desc->seq message: SSC::format(R"({
          "id": "$S",
          "result": "$i"
        })", std::to_string(desc->id), (int)req->result)];

        delete desc;
        uv_fs_req_cleanup(req);
      });
    });

    if (err) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self resolve: seq message: SSC::format(R"({
          "err": {
            "message": "$S"
          }
        })", std::string(uv_strerror(err)))];
      });
    }
    loopCheck();
  });
};

- (void) fsMkDir: (std::string)seq path: (std::string)path mode: (int)mode {
  dispatch_async(queue, ^{
    uv_fs_t req;
    DescriptorContext* desc = new DescriptorContext;
    desc->seq = seq;
    desc->delegate = self;
    req.data = desc;

    int err = uv_fs_mkdir(loop, &req, (const char*) path.c_str(), mode, [](uv_fs_t* req) {
      auto desc = static_cast<DescriptorContext*>(req->data);

      dispatch_async(dispatch_get_main_queue(), ^{
        [desc->delegate resolve: desc->seq message: SSC::format(R"({
          "id": "$S",
          "result": "$i"
        })", std::to_string(desc->id), (int)req->result)];

        delete desc;
        uv_fs_req_cleanup(req);
      });
    });

    if (err) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self resolve: seq message: SSC::format(R"({
          "err": {
            "message": "$S"
          }
        })", std::string(uv_strerror(err)))];
      });
    }
    loopCheck();
  });
};

- (void) fsReadDir: (std::string)seq path: (std::string)path {
  dispatch_async(queue, ^{
    DirectoryReader* ctx = new DirectoryReader;
    ctx->delegate = self;
    ctx->seq = seq;

    ctx->reqOpendir.data = ctx;
    ctx->reqReaddir.data = ctx;

    int err = uv_fs_opendir(loop, &ctx->reqOpendir, (const char*) path.c_str(), nullptr);

    if (err) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self resolve: seq message: SSC::format(R"({
          "err": {
            "message": "$S"
          }
        })", std::string(uv_strerror(err)))];
      });
      return;
    }

    err = uv_fs_readdir(loop, &ctx->reqReaddir, ctx->dir, [](uv_fs_t* req) {
      auto ctx = static_cast<DirectoryReader*>(req->data);

      if (req->result < 0) {
        dispatch_async(dispatch_get_main_queue(), ^{
          [ctx->delegate resolve: ctx->seq message: SSC::format(R"({
            "err": {
              "message": "$S"
            }
          })", std::string(uv_strerror((int)req->result)))];
        });
        return;
      }

      std::stringstream value;
      auto len = ctx->dir->nentries;

      for (int i = 0; i < len; i++) {
        value << "\"" << ctx->dir->dirents[i].name << "\"";

        if (i < len - 1) {
          // Assumes the user does not use commas in their file names.
          value << ",";
        }
      }

      NSString* str = [NSString stringWithUTF8String: value.str().c_str()];
      NSData *nsdata = [str dataUsingEncoding: NSUTF8StringEncoding];
      NSString *base64Encoded = [nsdata base64EncodedStringWithOptions:0];

      dispatch_async(dispatch_get_main_queue(), ^{
        [ctx->delegate resolve: ctx->seq message: SSC::format(R"({
          "data": "$S"
        })", std::string([base64Encoded UTF8String]))];
      });

      uv_fs_t reqClosedir;
      uv_fs_closedir(loop, &reqClosedir, ctx->dir, [](uv_fs_t* req) {
        uv_fs_req_cleanup(req);
      });
    });

    if (err) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self resolve: seq message: SSC::format(R"({
          "err": {
            "message": "$S"
          }
        })", std::string(uv_strerror(err)))];
      });
    }
    loopCheck();
  });
};

//
// TCP Methods
//
- (void) sendBufferSize: (std::string)seq clientId: (uint64_t)clientId size: (int)size {
  dispatch_async(queue, ^{
    Client* client = clients[clientId];

    if (client == nullptr) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self resolve: seq message: SSC::format(R"JSON({
          "err": {
            "message": "Not connected"
          }
        })JSON")];
      });
      return;
    }

    client->delegate = self;

    uv_handle_t* handle;

    if (client->tcp != nullptr) {
      handle = (uv_handle_t*) client->tcp;
    } else {
      handle = (uv_handle_t*) client->udp;
    }

    int sz = size;
    int rSize = uv_send_buffer_size(handle, &sz);

    dispatch_async(dispatch_get_main_queue(), ^{
      [self resolve: seq message: SSC::format(R"JSON({
        "data": {
          "size": $i
        }
      })JSON", rSize)];
    });
    return;
  });
}

- (void) recvBufferSize: (std::string)seq clientId: (uint64_t)clientId size: (int)size {
  dispatch_async(queue, ^{
    Client* client = clients[clientId];

    if (client == nullptr) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self resolve: seq message: SSC::format(R"JSON({
          "err": {
            "message": "Not connected"
          }
        })JSON")];
      });
      return;
    }

    client->delegate = self;

    uv_handle_t* handle;

    if (client->tcp != nullptr) {
      handle = (uv_handle_t*) client->tcp;
    } else {
      handle = (uv_handle_t*) client->udp;
    }

    int sz = size;
    int rSize = uv_recv_buffer_size(handle, &sz);

    dispatch_async(dispatch_get_main_queue(), ^{
      [self resolve: seq message: SSC::format(R"JSON({
        "data": {
          "size": $i
        }
      })JSON", rSize)];
    });
    return;
  });
}

- (void) tcpSend: (uint64_t)clientId message: (std::string)message {
  dispatch_async(queue, ^{
    Client* client = clients[clientId];

    if (client == nullptr) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self emit: "error" message: SSC::format(R"JSON({
          "clientId": "$S",
          "data": {
            "message": "Not connected"
          }
        })JSON", std::to_string(clientId))];
      });
      return;
    }

    client->delegate = self;

    write_req_t *wr = (write_req_t*) malloc(sizeof(write_req_t));
    wr->req.data = client;
    wr->buf = uv_buf_init((char* const) message.c_str(), (int) message.size());

    auto onWrite = [](uv_write_t *req, int status) {
      auto client = reinterpret_cast<Client*>(req->data);

      if (status) {
        dispatch_async(dispatch_get_main_queue(), ^{
          [client->delegate emit: "error" message: SSC::format(R"({
            "clientId": "$S",
            "data": {
              "message": "Write error $S"
            }
          })", std::to_string(client->clientId), uv_strerror(status))];
        });
        return;
      }

      write_req_t *wr = (write_req_t*) req;
      free(wr->buf.base);
      free(wr);
    };

    uv_write((uv_write_t*) wr, (uv_stream_t*) client->tcp, &wr->buf, 1, onWrite);
    loopCheck();
  });
}

- (void) tcpConnect: (std::string)seq clientId: (uint64_t)clientId port: (int)port ip: (std::string)ip {
  dispatch_async(queue, ^{
    uv_connect_t connect;

    Client* client = clients[clientId] = new Client();

    client->delegate = self;
    client->clientId = clientId;
    client->tcp = new uv_tcp_t;

    uv_tcp_init(loop, client->tcp);

    client->tcp->data = client;

    uv_tcp_nodelay(client->tcp, 0);
    uv_tcp_keepalive(client->tcp, 1, 60);

    struct sockaddr_in dest4;
    struct sockaddr_in6 dest6;

    // check to validate the ip is actually an ipv6 address with a regex
    if (ip.find(":") != std::string::npos) {
      uv_ip6_addr(ip.c_str(), port, &dest6);
    } else {
      uv_ip4_addr(ip.c_str(), port, &dest4);
    }

    // uv_ip4_addr("172.217.16.46", 80, &dest);

    NSLog(@"connect? %s:%i", ip.c_str(), port);

    auto onConnect = [](uv_connect_t* connect, int status) {
      auto* client = reinterpret_cast<Client*>(connect->handle->data);

      NSLog(@"client connection?");

      if (status < 0) {
        dispatch_async(dispatch_get_main_queue(), ^{
          [client->delegate resolve: client->seq message: SSC::format(R"({
            "err": {
              "clientId": "$S",
              "message": "$S"
            }
          })", std::to_string(client->clientId), std::string(uv_strerror(status)))];
        });
        return;
      }

      dispatch_async(dispatch_get_main_queue(), ^{
        [client->delegate resolve: client->seq message: SSC::format(R"({
          "data": {
            "clientId": "$S"
          }
        })", std::to_string(client->clientId))];
      });

      auto onRead = [](uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf) {
        auto client = reinterpret_cast<Client*>(handle->data);

        NSString* str = [NSString stringWithUTF8String: buf->base];
        NSData *nsdata = [str dataUsingEncoding:NSUTF8StringEncoding];
        NSString *base64Encoded = [nsdata base64EncodedStringWithOptions:0];

        auto clientId = std::to_string(client->clientId);
        auto message = std::string([base64Encoded UTF8String]);

        dispatch_async(dispatch_get_main_queue(), ^{
          [client->delegate emit: "data" message: SSC::format(R"({
            "clientId": "$S",
            "data": "$S"
          })", clientId, message)];
        });
      };

      auto allocate = [](uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
        buf->base = (char*) malloc(suggested_size);
        buf->len = suggested_size;
      };

      uv_read_start((uv_stream_t*) connect->handle, allocate, onRead);
    };

    int r = 0;

    if (ip.find(":") != std::string::npos) {
      r = uv_tcp_connect(&connect, client->tcp, (const struct sockaddr*) &dest6, onConnect);
    } else {
      r = uv_tcp_connect(&connect, client->tcp, (const struct sockaddr*) &dest4, onConnect);
    }

    if (r) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self resolve: seq message: SSC::format(R"({
          "err": {
            "clientId": "$S",
            "message": "$S"
          }
        })", std::to_string(clientId), std::string(uv_strerror(r)))];
      });
      return;
    }

    loopCheck();
  });
}

- (void) tcpBind: (std::string) seq serverId: (uint64_t)serverId ip: (std::string)ip port: (int)port {
  dispatch_async(queue, ^{
    loop = uv_default_loop();

    Server* server = servers[serverId] = new Server();
    server->tcp = new uv_tcp_t;
    server->delegate = self;
    server->serverId = serverId;
    server->tcp->data = &server;

    uv_tcp_init(loop, server->tcp);
    struct sockaddr_in addr;

    // addr.sin_port = htons(port);
    // addr.sin_addr.s_addr = htonl(INADDR_ANY);
    // NSLog(@"LISTENING %i", addr.sin_addr.s_addr);
    // NSLog(@"LISTENING %s:%i", ip.c_str(), port);

    uv_ip4_addr(ip.c_str(), port, &addr);
    uv_tcp_simultaneous_accepts(server->tcp, 0);
    uv_tcp_bind(server->tcp, (const struct sockaddr*) &addr, 0);

    int r = uv_listen((uv_stream_t*) &server, DEFAULT_BACKLOG, [](uv_stream_t* handle, int status) {
      auto* server = reinterpret_cast<Server*>(handle->data);

      NSLog(@"connection?");

      if (status < 0) {
        dispatch_async(dispatch_get_main_queue(), ^{
          [server->delegate emit: "connection" message: SSC::format(R"JSON({
            "serverId": "$S",
            "data": "New connection error $S"
          })JSON", std::to_string(server->serverId), uv_strerror(status))];
        });
        return;
      }

      auto clientId = SSC::rand64();
      Client* client = clients[clientId] = new Client();
      client->clientId = clientId;
      client->server = server;
      client->stream = handle;
      client->tcp = new uv_tcp_t;

      client->tcp->data = client;

      uv_tcp_init(loop, client->tcp);

      if (uv_accept(handle, (uv_stream_t*) handle) == 0) {
        PeerInfo info;
        info.init(client->tcp);

        dispatch_async(dispatch_get_main_queue(), ^{
          [server->delegate
           emit: "connection"
           message: SSC::format(R"JSON({
             "serverId": "$S",
             "clientId": "$S",
             "data": {
               "ip": "$S",
               "family": "$S",
               "port": "$i"
             }
            })JSON",
            std::to_string(server->serverId),
            std::to_string(clientId),
            info.ip,
            info.family,
            info.port
          )];
        });
      } else {
        uv_close((uv_handle_t*) handle, [](uv_handle_t* handle) {
          free(handle);
        });
      }
    });

    if (r) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self resolve: seq message: SSC::format(R"JSON({
          "err": {
            "serverId": "$S",
            "message": "$S"
          }
        })JSON", std::to_string(server->serverId), std::string(uv_strerror(r)))];
      });

      NSLog(@"Listener failed: %s", uv_strerror(r));
      return;
    }

    dispatch_async(dispatch_get_main_queue(), ^{
      [self resolve: seq message: SSC::format(R"JSON({
        "data": {
          "serverId": "$S",
          "port": "$i",
          "ip": "$S"
        }
      })JSON", std::to_string(server->serverId), port, ip)];
      NSLog(@"Listener started");
    });

    loopCheck();
  });
}

- (void) tcpSetKeepAlive: (std::string)seq clientId: (uint64_t)clientId timeout: (int)timeout {
  dispatch_async(queue, ^{
    Client* client = clients[clientId];

    if (client == nullptr) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self resolve:seq message: SSC::format(R"JSON({
          "err": {
            "clientId": "$S",
            "message": "No connection found with the specified id"
          }
        })JSON", std::to_string(clientId))];
      });
      return;
    }

    client->seq = seq;
    client->delegate = self;
    client->clientId = clientId;

    uv_tcp_keepalive((uv_tcp_t*) client->tcp, 1, timeout);

    dispatch_async(dispatch_get_main_queue(), ^{
      [client->delegate resolve:client->seq message: SSC::format(R"JSON({
        "data": {}
      })JSON")];
    });
  });
}

- (void) tcpSetTimeout: (std::string)seq clientId: (uint64_t)clientId timeout: (int)timeout {
  // TODO
}

- (void) tcpReadStart: (std::string)seq clientId: (uint64_t)clientId {
  dispatch_async(queue, ^{
    Client* client = clients[clientId];

    if (client == nullptr) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self resolve:seq message: SSC::format(R"JSON({
          "err": {
            "clientId": "$S",
            "message": "No connection found with the specified id"
          }
        })JSON", std::to_string(clientId))];
      });
      return;
    }

    client->seq = seq;
    client->delegate = self;

    auto onRead = [](uv_stream_t* handle, ssize_t nread, const uv_buf_t *buf) {
      auto client = reinterpret_cast<Client*>(handle->data);

      if (nread > 0) {
        write_req_t *req = (write_req_t*) malloc(sizeof(write_req_t));
        req->buf = uv_buf_init(buf->base, (int) nread);

        NSString* str = [NSString stringWithUTF8String: req->buf.base];
        NSData *nsdata = [str dataUsingEncoding:NSUTF8StringEncoding];
        NSString *base64Encoded = [nsdata base64EncodedStringWithOptions:0];

        auto serverId = std::to_string(client->server->serverId);
        auto clientId = std::to_string(client->clientId);
        auto message = std::string([base64Encoded UTF8String]);

        dispatch_async(dispatch_get_main_queue(), ^{
          [client->server->delegate emit: "data" message: SSC::format(R"JSON({
            "serverId": "$S",
            "clientId": "$S",
            "data": "$S"
          })JSON", serverId, clientId, message)];
        });
        return;
      }

      if (nread < 0) {
        if (nread != UV_EOF) {
          dispatch_async(dispatch_get_main_queue(), ^{
            [client->server->delegate emit: "error" message: SSC::format(R"JSON({
              "serverId": "$S",
              "data": "$S"
            })JSON", std::to_string(client->server->serverId), uv_err_name((int) nread))];
          });
        }

        uv_close((uv_handle_t*) client->tcp, [](uv_handle_t* handle) {
          free(handle);
        });
      }

      free(buf->base);
    };

    auto allocateBuffer = [](uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
      buf->base = (char*) malloc(suggested_size);
      buf->len = suggested_size;
    };

    int err = uv_read_start((uv_stream_t*) client->stream, allocateBuffer, onRead);

    if (err < 0) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self resolve:seq message: SSC::format(R"JSON({
          "err": {
            "serverId": "$S",
            "message": "$S"
          }
        })JSON", std::to_string(client->server->serverId), uv_strerror(err))];
      });
      return;
    }

    dispatch_async(dispatch_get_main_queue(), ^{
      [self resolve:client->server->seq message: SSC::format(R"JSON({
        "data": {}
      })JSON")];
    });

    loopCheck();
  });
}

- (void) readStop: (std::string)seq clientId:(uint64_t)clientId {
  dispatch_async(queue, ^{
    Client* client = clients[clientId];

    if (client == nullptr) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self resolve:seq message: SSC::format(R"JSON({
          "err": {
            "clientId": "$S",
            "message": "No connection with specified id"
          }
        })JSON", std::to_string(clientId))];
      });
      return;
    }

    int r;

    if (client->tcp) {
      r = uv_read_stop((uv_stream_t*) client->tcp);
    } else {
      r = uv_read_stop((uv_stream_t*) client->udp);
    }

    dispatch_async(dispatch_get_main_queue(), ^{
      [self resolve:client->seq message: SSC::format(R"JSON({
        "data": $i
      })JSON", r)];
    });
  });
}

- (void) close: (std::string)seq clientId:(uint64_t)clientId {
  dispatch_async(queue, ^{
    Client* client = clients[clientId];

    if (client == nullptr) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self resolve:seq message: SSC::format(R"JSON({
          "err": {
            "clientId": "$S",
            "message": "No connection with specified id"
          }
        })JSON", std::to_string(clientId))];
      });
      return;
    }

    client->seq = seq;
    client->delegate = self;
    client->clientId = clientId;

    uv_handle_t* handle;

    if (client->tcp != nullptr) {
      handle = (uv_handle_t*) client->tcp;
    } else {
      handle = (uv_handle_t*) client->udp;
    }

    handle->data = client;

    uv_close(handle, [](uv_handle_t* handle) {
      auto client = reinterpret_cast<Client*>(handle->data);

      dispatch_async(dispatch_get_main_queue(), ^{
        [client->delegate resolve:client->seq message: SSC::format(R"JSON({
          "data": {}
        })JSON")];
      });

      free(handle);
    });
    loopCheck();
  });
}

- (void) shutdown: (std::string) seq clientId: (uint64_t)clientId {
  dispatch_async(queue, ^{
    Client* client = clients[clientId];

    if (client == nullptr) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self resolve:seq message: SSC::format(R"JSON({
          "err": {
            "clientId": "$S",
            "message": "No connection with specified id"
          }
        })JSON", std::to_string(clientId))];
      });
      return;
    }

    client->seq = seq;
    client->delegate = self;
    client->clientId = clientId;

    uv_handle_t* handle;

    if (client->tcp != nullptr) {
      handle = (uv_handle_t*) client->tcp;
    } else {
      handle = (uv_handle_t*) client->udp;
    }

    handle->data = client;

    uv_shutdown_t *req = new uv_shutdown_t;
    req->data = handle;

    uv_shutdown(req, (uv_stream_t*) handle, [](uv_shutdown_t *req, int status) {
      auto client = reinterpret_cast<Client*>(req->handle->data);

      dispatch_async(dispatch_get_main_queue(), ^{
        [client->delegate resolve:client->seq message: SSC::format(R"JSON({
          "data": {
            "status": "$i"
          }
        })JSON", status)];
      });

      free(req);
      free(req->handle);
    });
    loopCheck();
  });
}

//
// UDP Methods
//
- (void) udpBind: (std::string)seq serverId: (uint64_t)serverId ip: (std::string)ip port: (int)port {
  dispatch_async(queue, ^{
    loop = uv_default_loop();
    Server* server = servers[serverId] = new Server();
    server->udp = new uv_udp_t;
    server->seq = seq;
    server->serverId = serverId;
    server->delegate = self;
    server->udp->data = server;

    int err;
    struct sockaddr_in addr;

    err = uv_ip4_addr((char *) ip.c_str(), port, &addr);

    if (err < 0) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self resolve:seq message: SSC::format(R"JSON({
          "err": {
            "serverId": "$S",
            "message": "$S"
          }
        })JSON", std::to_string(serverId), uv_strerror(err))];
      });
      return;
    }

    err = uv_udp_bind(server->udp, (const struct sockaddr*) &addr, 0);

    if (err < 0) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [server->delegate emit: "error" message: SSC::format(R"JSON({
          "serverId": "$S",
          "data": "$S"
        })JSON", std::to_string(server->serverId), uv_strerror(err))];
      });
      return;
    }

    dispatch_async(dispatch_get_main_queue(), ^{
      [server->delegate resolve:server->seq message: SSC::format(R"JSON({
        "data": {}
      })JSON")];
    });

    loopCheck();
  });
}

- (void) udpSend: (std::string)seq
        clientId: (uint64_t)clientId
         message: (std::string)message
          offset: (int)offset
             len: (int)len
            port: (int)port
              ip: (const char*)ip {
  dispatch_async(queue, ^{
    Client* client = clients[clientId];

    if (client == nullptr) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self resolve:seq message: SSC::format(R"JSON({
          "err": {
            "clientId": "$S",
            "message": "no such client"
          }
        })JSON", std::to_string(clientId))];
      });
      return;
    }

    client->delegate = self;
    client->seq = seq;

    int err;
    uv_udp_send_t* req = new uv_udp_send_t;
    req->data = client;

    err = uv_ip4_addr((char *) ip, port, &addr);

    if (err) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self resolve:seq message: SSC::format(R"JSON({
          "err": {
            "clientId": "$S",
            "message": "$S"
          }
        })JSON", std::to_string(clientId), uv_strerror(err))];
      });
      return;
    }

    uv_buf_t bufs[1];
    char* base = (char*) message.c_str();
    bufs[0] = uv_buf_init(base + offset, len);

    err = uv_udp_send(req, client->udp, bufs, 1, (const struct sockaddr *) &addr, [] (uv_udp_send_t *req, int status) {
      auto client = reinterpret_cast<Client*>(req->data);

      dispatch_async(dispatch_get_main_queue(), ^{
        [client->delegate resolve:client->seq message: SSC::format(R"JSON({
          "data": {
            "clientId": "$S",
            "status": "$i"
          }
        })JSON", std::to_string(client->clientId), status)];
      });

      delete[] req->bufs;
      free(req);
    });

    if (err) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [client->delegate emit: "error" message: SSC::format(R"({
          "clientId": "$S",
          "data": {
            "message": "Write error $S"
          }
        })", std::to_string(client->clientId), uv_strerror(err))];
      });
      return;
    }
    loopCheck();
  });
}

- (void) udpReadStart: (std::string)seq serverId: (uint64_t)serverId {
  dispatch_async(queue, ^{
    Server* server = servers[serverId];

    if (server == nullptr) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self resolve:seq message: SSC::format(R"JSON({
          "err": {
            "serverId": "$S",
            "message": "no such server"
          }
        })JSON", std::to_string(serverId))];
      });
      return;
    }

    server->delegate = self;
    server->seq = seq;

    auto allocate = [](uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
      buf->base = (char*) malloc(suggested_size);
      buf->len = suggested_size;
    };

    int err = uv_udp_recv_start(server->udp, allocate, [](uv_udp_t *handle, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags) {
      Server *server = (Server*) handle->data;

      if (nread > 0) {
        int port;
        char ipbuf[17];
        std::string data(buf->base);
        parseAddress((struct sockaddr *) addr, &port, ipbuf);
        std::string ip(ipbuf);

        dispatch_async(dispatch_get_main_queue(), ^{
          [server->delegate emit: "data" message: SSC::format(R"JSON({
            "serverId": "$S",
            "port": "$i",
            "ip": "$S",
            "data": "$S"
          })JSON", std::to_string(server->serverId), port, ip, data)];
        });
        return;
      }
    });

    if (err < 0) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self resolve:seq message: SSC::format(R"JSON({
          "err": {
            "serverId": "$S",
            "message": "$S"
          }
        })JSON", std::to_string(serverId), uv_strerror(err))];
      });
      return;
    }

    dispatch_async(dispatch_get_main_queue(), ^{
      [server->delegate resolve:server->seq message: SSC::format(R"JSON({
        "data": {}
      })JSON")];
    });
    loopCheck();
  });
}

//
// Network Methods
//
- (void) dnsLookup: (std::string)seq
          hostname: (std::string)hostname {
  dispatch_async(queue, ^{
    loop = uv_default_loop();
    auto ctxId = SSC::rand64();
    GenericContext* ctx = contexts[ctxId] = new GenericContext;
    ctx->id = ctxId;
    ctx->delegate = self;
    ctx->seq = seq;

    struct addrinfo hints;
    hints.ai_family = PF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = 0;

    uv_getaddrinfo_t* resolver = new uv_getaddrinfo_t;
    resolver->data = ctx;

    uv_getaddrinfo(loop, resolver, [](uv_getaddrinfo_t *resolver, int status, struct addrinfo *res) {
      auto ctx = (GenericContext*) resolver->data;

      if (status < 0) {
        [ctx->delegate resolve: ctx->seq message: SSC::format(R"JSON({
          "err": {
            "code": "$S",
            "message": "$S"
          }
        })JSON", std::string(uv_err_name((int) status)), std::string(uv_strerror(status)))];
        contexts.erase(ctx->id);
        return;
      }

      char addr[17] = {'\0'};
      uv_ip4_name((struct sockaddr_in*) res->ai_addr, addr, 16);
      std::string ip(addr, 17);

      dispatch_async(dispatch_get_main_queue(), ^{
        [ctx->delegate resolve: ctx->seq message: SSC::format(R"JSON({
          "data": "$S"
        })JSON", ip)];

        contexts.erase(ctx->id);
      });

      uv_freeaddrinfo(res);
    }, hostname.c_str(), nullptr, &hints);

    loopCheck();
  });
}

- (void) initNetworkStatusObserver {
  dispatch_queue_attr_t attrs = dispatch_queue_attr_make_with_qos_class(
    DISPATCH_QUEUE_SERIAL,
    QOS_CLASS_UTILITY,
    DISPATCH_QUEUE_PRIORITY_DEFAULT
  );

  self.monitorQueue = dispatch_queue_create("com.example.network-monitor", attrs);

  // self.monitor = nw_path_monitor_create_with_type(nw_interface_type_wifi);
  self.monitor = nw_path_monitor_create();
  nw_path_monitor_set_queue(self.monitor, self.monitorQueue);
  nw_path_monitor_set_update_handler(self.monitor, ^(nw_path_t _Nonnull path) {
    nw_path_status_t status = nw_path_get_status(path);

    std::string name;
    std::string message;

    switch (status) {
      case nw_path_status_invalid: {
        name = "offline";
        message = "Network path is invalid";
        break;
      }
      case nw_path_status_satisfied: {
        name = "online";
        message = "Network is usable";
        break;
      }
      case nw_path_status_satisfiable: {
        name = "online";
        message = "Network may be usable";
        break;
      }
      case nw_path_status_unsatisfied: {
        name = "offline";
        message = "Network is not usable";
        break;
      }
    }

    dispatch_async(dispatch_get_main_queue(), ^{
      [self emit: name message: SSC::format(R"JSON({
        "message": "$S"
      })JSON", message)];
    });
  });

  nw_path_monitor_start(self.monitor);
}

- (std::string) getNetworkInterfaces {
  struct ifaddrs *interfaces = nullptr;
  struct ifaddrs *interface = nullptr;
  int success = getifaddrs(&interfaces);
  std::stringstream value;
  std::stringstream v4;
  std::stringstream v6;

  if (success != 0) {
    return "{\"err\": {\"message\":\"unable to get interfaces\"}}";
  }

  interface = interfaces;
  v4 << "\"ipv4\":{";
  v6 << "\"ipv6\":{";

  while (interface != nullptr) {
    std::string ip = "";
    const struct sockaddr_in *addr = (const struct sockaddr_in*)interface->ifa_addr;

    if (addr->sin_family == AF_INET) {
      struct sockaddr_in *addr = (struct sockaddr_in*)interface->ifa_addr;
      v4 << "\"" << interface->ifa_name << "\":\"" << addrToIPv4(addr) << "\",";
    }

    if (addr->sin_family == AF_INET6) {
      struct sockaddr_in6 *addr = (struct sockaddr_in6*)interface->ifa_addr;
      v6 << "\"" << interface->ifa_name << "\":\"" << addrToIPv6(addr) << "\",";
    }

    interface = interface->ifa_next;
  }

  v4 << "\"local\":\"0.0.0.0\"}";
  v6 << "\"local\":\"::1\"}";

  getifaddrs(&interfaces);
  freeifaddrs(interfaces);

  value << "{\"data\":{" << v4.str() << "," << v6.str() << "}}";
  return value.str();
}
@end

#endif /* APPLE_SSC_H */