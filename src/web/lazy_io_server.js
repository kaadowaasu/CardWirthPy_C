// CARDWIRTH_WEB_IO_SERVER 用の、ブラウザ
// ーメインスレッド上の非同期 I/O サーバー。PROXY_TO_PTHREAD
// は全 C++ アプリコードを pthread で動かす。メ
// インへの同期代理呼び出しと非同期実装を組み合わせ、PROXY_SYNC_ASYNC
// で要求元ワーカーだけを待たせ、ブラウザーメインは IndexedDB
// の Promise を処理できるようにする。境界を越
// えた拒否でワーカーが待ち続けないよう、同期例外と Promise
// の失敗を捕捉する。公開関数は同期のままにする。async
// にすると、Emscripten が外側に挿入する
// 代理の戻り値 errno が Promise に包
// まれ、Wasm がゼロに変換して偽の成功になる。
addToLibrary({
  // 二つの検査と名前変更は、ファイルシステムの所有スレッド上で await を挟まずに行う。
  cardwirth_rename_no_replace__deps: ["$FS", "$UTF8ToString"],
  cardwirth_rename_no_replace__proxy: "sync",
  cardwirth_rename_no_replace: (source, destination) => {
    try {
      source = UTF8ToString(source);
      destination = UTF8ToString(destination);
      try {
        FS.lstat(destination);
        return {{{ cDefs.EEXIST }}};
      } catch (error) {
        if (error.errno !== {{{ cDefs.ENOENT }}}) throw error;
      }
      FS.rename(source, destination);
      return 0;
    } catch (error) { return error.errno || {{{ cDefs.EIO }}}; }
  },

  $cardwirthSaveRequests: '({next: 0, results: new Map()})',
  cardwirth_persist_begin__deps: ["$cardwirthSaveRequests"],
  cardwirth_persist_begin__proxy: "sync",
  cardwirth_persist_begin: () => {
    const id = ++cardwirthSaveRequests.next;
    cardwirthSaveRequests.results.set(id, 0);
    const timeout = setTimeout(() => {
      if (cardwirthSaveRequests.results.get(id) === 0)
        cardwirthSaveRequests.results.set(id, -2);
    }, 30_000);
    const settle = (result) => {
      if (cardwirthSaveRequests.results.get(id) !== 0) return;
      clearTimeout(timeout);
      cardwirthSaveRequests.results.set(id, result);
    };
    Promise.resolve().then(() => {
      if (!Module.cardwirthFlushUserStorage) throw new Error('User storage flush is unavailable');
      return Module.cardwirthFlushUserStorage();
    }).then(() => settle(1), () => settle(-1));
    return id;
  },
  cardwirth_persist_poll__deps: ["$cardwirthSaveRequests"],
  cardwirth_persist_poll__proxy: "sync",
  cardwirth_persist_poll: (id) => {
    const result = cardwirthSaveRequests.results.get(id) ?? -1;
    if (result) cardwirthSaveRequests.results.delete(id);
    return result;
  },
  $cardwirthStreamQueues: 'new WeakMap()',
  $cardwirthMappings: 'new Map()',
  $cardwirthStreamOperation__deps: ["$cardwirthStreamQueues", "$SYSCALLS", "$FS"],
  $cardwirthStreamOperation: (fd, stream, action, requireOpen = true) => {
    // 別々の open() でも通常ファイルの内容・EOF は共有する。ノー
    // ド単位でキューに並べ、O_APPEND の EOF 取得と待機を伴う書
    // き込みを一操作にする。位置は各オープン状態に保持し、dup() はそ
    // の状態を共有する。非ファイルはオープン状態単位の順序を保ち、パイプの
    // 読み取り待ちが別の書き込み側を同じノードの後ろで止めないようにする。
    const owner = stream.node && FS.isFile(stream.node.mode)
      ? stream.node : stream.shared || stream;
    const previous = cardwirthStreamQueues.get(owner) || Promise.resolve();
    const result = previous.then(() => {
      if (requireOpen && SYSCALLS.getStreamFromFD(fd) !== stream) throw new FS.ErrnoError({{{ cDefs.EBADF }}});
      return action();
    });
    const tail = result.then(() => {}, () => {});
    cardwirthStreamQueues.set(owner, tail);
    tail.then(() => {
      if (cardwirthStreamQueues.get(owner) === tail) cardwirthStreamQueues.delete(owner);
    });
    return result;
  },

  fd_read__deps: ["$SYSCALLS", "$FS", "$cardwirthStreamOperation"],
  fd_read__proxy: "sync",
  fd_read__async: "auto",
  // wasm32 の i32 ポインターは JavaScript に符号付き数値で渡る。旧
  // >> と異なり、>>> で 3 GiB ヒープの 2 GiB 超のアドレスを保つ。
  fd_read: (fd, iov, iovcnt, pnum) => { return (async () => {
    try {
      const stream = SYSCALLS.getStreamFromFD(fd);
      return await cardwirthStreamOperation(fd, stream, async () => {
      if (!stream.isRead) return {{{ cDefs.EBADF }}};
      if (FS.isDir(stream.node.mode)) return {{{ cDefs.EISDIR }}};
      if (!stream.stream_ops.read) return {{{ cDefs.EINVAL }}};
      let total = 0;
      for (let i = 0; i < iovcnt; ++i, iov += 8) {
        const pointer = HEAPU32[iov >>> 2];
        const length = HEAPU32[(iov + 4) >>> 2];
        const position = stream.position;
        const count = await stream.stream_ops.read(
          stream, HEAP8, pointer, length, position);
        if (count < 0) return {{{ cDefs.EIO }}};
        stream.position += count;
        total += count;
        if (count < length) break;
      }
      HEAPU32[pnum >>> 2] = total;
      return 0;
      });
    } catch (error) {
      if (typeof FS !== "undefined" && error instanceof FS.ErrnoError) return error.errno;
      console.error(`fd_read: ${error && error.message ? error.message : error}`);
      return {{{ cDefs.EIO }}};
    }
  })(); },

  fd_write__deps: ["$SYSCALLS", "$FS", "$cardwirthStreamOperation"],
  fd_write__proxy: "sync",
  fd_write__async: "auto",
  fd_write: (fd, iov, iovcnt, pnum) => { return (async () => {
    try {
      const stream = SYSCALLS.getStreamFromFD(fd);
      return await cardwirthStreamOperation(fd, stream, async () => {
      if (!stream.isWrite) return {{{ cDefs.EBADF }}};
      if (FS.isDir(stream.node.mode)) return {{{ cDefs.EISDIR }}};
      if (!stream.stream_ops.write) return {{{ cDefs.EINVAL }}};
      let total = 0;
      for (let i = 0; i < iovcnt; ++i, iov += 8) {
        const pointer = HEAPU32[iov >>> 2];
        const length = HEAPU32[(iov + 4) >>> 2];
        if (stream.seekable && (stream.flags & 1024)) FS.llseek(stream, 0, 2);
        const position = stream.position;
        const count = await stream.stream_ops.write(
          stream, HEAP8, pointer, length, position, false);
        if (count < 0) return {{{ cDefs.EIO }}};
        stream.position += count;
        // 待機を伴う書き込みの後に、接続中のノードを解決する。名前変更でパスが変
        // わったり、unlink で保存の所有者が外れたりしている場合がある。
        Module["cardwirthMarkDirtyFile"]?.(stream.path, stream.node);
        total += count;
        if (count < length) break;
      }
      HEAPU32[pnum >>> 2] = total;
      return 0;
      });
    } catch (error) {
      if (typeof FS !== "undefined" && error instanceof FS.ErrnoError) return error.errno;
      console.error(`fd_write: ${error && error.message ? error.message : error}`);
      return {{{ cDefs.EIO }}};
    }
  })(); },

  $cardwirthPositionedIO__deps: ["$SYSCALLS", "$FS", "$cardwirthStreamOperation"],
  $cardwirthPositionedIO: async (fd, iov, iovcnt, offset, pnum, writing) => {
    try {
      if (isNaN(offset)) return {{{ cDefs.EFBIG }}};
      const stream = SYSCALLS.getStreamFromFD(fd);
      return await cardwirthStreamOperation(fd, stream, async () => {
        let total = 0;
        for (let i = 0; i < iovcnt; ++i, iov += 8) {
          const pointer = HEAPU32[iov >>> 2];
          const length = HEAPU32[(iov + 4) >>> 2];
          // SDK の権限・シーク可否と O_APPEND の処理を
          // 保つ。明示オフセットでは通常の読み書き位置を進めない。
          const count = writing
            ? await FS.write(stream, HEAP8, pointer, length, offset, false)
            : await FS.read(stream, HEAP8, pointer, length, offset);
          if (count < 0) return {{{ cDefs.EIO }}};
          offset += count;
          total += count;
          if (count < length) break;
        }
        HEAPU32[pnum >>> 2] = total;
        return 0;
      });
    } catch (error) {
      if (error instanceof FS.ErrnoError) return error.errno;
      console.error(`positioned I/O: ${error && error.message ? error.message : error}`);
      return {{{ cDefs.EIO }}};
    }
  },

  fd_pread__deps: ["$cardwirthPositionedIO"],
  fd_pread__i53abi: true,
  fd_pread__proxy: "sync",
  fd_pread__async: "auto",
  fd_pread: (fd, iov, iovcnt, offset, pnum) => {
    return cardwirthPositionedIO(fd, iov, iovcnt, offset, pnum, false);
  },

  fd_pwrite__deps: ["$cardwirthPositionedIO"],
  fd_pwrite__i53abi: true,
  fd_pwrite__proxy: "sync",
  fd_pwrite__async: "auto",
  fd_pwrite: (fd, iov, iovcnt, offset, pnum) => {
    return cardwirthPositionedIO(fd, iov, iovcnt, offset, pnum, true);
  },

  fd_seek__deps: ["$SYSCALLS", "$FS", "$cardwirthStreamOperation"],
  fd_seek__i53abi: true,
  fd_seek__proxy: "sync",
  fd_seek__async: "auto",
  fd_seek: (fd, offset, whence, newOffset) => { return (async () => {
    try {
      if (isNaN(offset)) return {{{ cDefs.EFBIG }}};
      const stream = SYSCALLS.getStreamFromFD(fd);
      return await cardwirthStreamOperation(fd, stream, () => {
        FS.llseek(stream, offset, whence);
        {{{ makeSetValue('newOffset', '0', 'stream.position', 'i64') }}};
        if (stream.getdents && !offset && whence === {{{ cDefs.SEEK_SET }}}) stream.getdents = null;
        return 0;
      });
    } catch (error) {
      if (error instanceof FS.ErrnoError) return error.errno;
      return {{{ cDefs.EIO }}};
    }
  })(); },

  // MEMFS に mount.syncfs はない。何もしない SDK の fd_sync
  // と異なり、UserData の fsync は共通 IndexedDB プロバイダーの確定
  // を待って応答する。Asyncify と pthread の両ビルドでこの一つの連携を使う。
  fd_sync__deps: ["$SYSCALLS", "$FS", "$cardwirthStreamOperation"],
  fd_sync__proxy: "sync",
  fd_sync__async: "auto",
  fd_sync: (fd) => { return (async () => {
    try {
      const stream = SYSCALLS.getStreamFromFD(fd);
      return await cardwirthStreamOperation(fd, stream, async () => {
        const result = await stream.stream_ops?.fsync?.(stream);
        if (result) return result;
        const path = FS.getPath(stream.node);
        if (path === '/user' || path.startsWith('/user/')) {
          if (typeof Module.cardwirthFlushUserStorage === 'function') {
            await Module.cardwirthFlushUserStorage();
          } else {
            // 共通の仮想 UserData プロバイダーを導入していないホス
            // トでは、SDK の実際の IDBFS のマウント処理を保つ。
            const mount = stream.node.mount;
            if (!mount?.type.syncfs) return {{{ cDefs.EIO }}};
            await new Promise((resolve, reject) => mount.type.syncfs(
              mount, false, error => error ? reject(error) : resolve()));
          }
        }
        return 0;
      });
    } catch (error) { return error.errno || {{{ cDefs.EIO }}}; }
  })(); },

  fd_close__deps: ["$SYSCALLS", "$FS", "$cardwirthStreamOperation"],
  fd_close__proxy: "sync",
  fd_close__async: "auto",
  fd_close: (fd) => { return (async () => {
    try {
      const stream = SYSCALLS.getStreamFromFD(fd);
      return await cardwirthStreamOperation(fd, stream, () => { FS.close(stream); return 0; });
    } catch (error) {
      if (error instanceof FS.ErrnoError) return error.errno;
      return {{{ cDefs.EIO }}};
    }
  })(); },

  _mmap_js__deps: ["$SYSCALLS", "$FS", "$cardwirthStreamOperation", "$cardwirthMappings", "$mmapAlloc", "emscripten_builtin_memalign", "emscripten_builtin_free"],
  _mmap_js__i53abi: true,
  _mmap_js__proxy: "sync",
  _mmap_js__async: "auto",
  _mmap_js: (length, protection, flags, fd, offset, allocated, address) => { return (async () => {
    try {
      const stream = SYSCALLS.getStreamFromFD(fd);
      // 遅延スナップショットが同じオープン状態の先行書き込みを追い越
      // したり、後の閉じる処理の完了後まで残ったりしないようにする。
      return await cardwirthStreamOperation(fd, stream, async () => {
      const result = await FS.mmap(stream, length, offset, protection, flags);
      // mmap は close(fd) 後もファイルのオープン状態を保持する。SDK の libc
      // の対応表は fd しか持たないため、再検索すると再利用された番号へ書き込む恐れがある。
      cardwirthMappings.set(result.ptr >>> 0, {stream, length, offset, protection, flags});
      HEAP32[allocated >>> 2] = result.allocated;
      HEAPU32[address >>> 2] = result.ptr;
      return 0;
      });
    } catch (error) {
      if (typeof FS !== "undefined" && error instanceof FS.ErrnoError) return -error.errno;
      console.error(`mmap: ${error && error.message ? error.message : error}`);
      return -{{{ cDefs.EIO }}};
    }
  })(); },

  $cardwirthMsync__deps: ["$FS"],
  $cardwirthMsync: async (address, stream, length, flags, offset) => {
    address >>>= 0; // Blob・型付き配列の範囲には符号なしの wasm32 アドレスが必要。SDK
    // の SYSCALLS.doMsync は FS.msync の戻り値を捨てるが、
    // 遅延保存では Promise の待機と、その後の変更済み状態の通知の両方を行う。
    if (!FS.isFile(stream.node.mode)) throw new FS.ErrnoError({{{ cDefs.ENODEV }}});
    if (flags & {{{ cDefs.MAP_PRIVATE }}}) return 0;
    const buffer = HEAPU8.slice(address, address + length);
    await FS.msync(stream, buffer, offset, length, flags);
    Module["cardwirthMarkDirtyFile"]?.(stream.path, stream.node);
    return 0;
  },

  _msync_js__deps: ["$FS", "$cardwirthStreamOperation", "$cardwirthMsync", "$cardwirthMappings"],
  _msync_js__i53abi: true,
  _msync_js__proxy: "sync",
  _msync_js__async: "auto",
  _msync_js: (address, length, protection, flags, fd, offset) => { return (async () => {
    try {
      address >>>= 0; // mmap が記録したものと同じ符号なしキーを使う。
      if (isNaN(offset)) return -{{{ cDefs.EFBIG }}};
      const mapping = cardwirthMappings.get(address);
      if (!mapping || length < 0 || length > mapping.length) return -{{{ cDefs.EINVAL }}};
      return await cardwirthStreamOperation(fd, mapping.stream,
        () => cardwirthMsync(address, mapping.stream, length, mapping.flags, mapping.offset), false);
    } catch (error) {
      if (error instanceof FS.ErrnoError) return -error.errno;
      return -{{{ cDefs.EIO }}};
    }
  })(); },

  _munmap_js__deps: ["$FS", "$cardwirthStreamOperation", "$cardwirthMsync", "$cardwirthMappings"],
  _munmap_js__i53abi: true,
  _munmap_js__proxy: "sync",
  _munmap_js__async: "auto",
  _munmap_js: (address, length, protection, flags, fd, offset) => { return (async () => {
    try {
      address >>>= 0; // mmap が記録したものと同じ符号なしキーを使う。
      const mapping = cardwirthMappings.get(address);
      if (!mapping || length !== mapping.length) return -{{{ cDefs.EINVAL }}};
      // SDK の libc は書き戻しエラーでも対応表を除き、確保領域を
      // 解放する。こちらの対応する保持も解除し、古いアドレスを残さない。
      cardwirthMappings.delete(address);
      return await cardwirthStreamOperation(fd, mapping.stream, async () => {
      if ((mapping.protection & 2) && !(mapping.flags & 2))
        await cardwirthMsync(address, mapping.stream, length, mapping.flags, mapping.offset);
      return 0;
      }, false);
    } catch (error) {
      if (typeof FS !== "undefined" && error instanceof FS.ErrnoError) return -error.errno;
      console.error(`munmap: ${error && error.message ? error.message : error}`);
      return -{{{ cDefs.EIO }}};
    }
  })(); }
});
