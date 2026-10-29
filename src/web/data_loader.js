// Emscripten の --pre-js ファイル。C++ には /user 以下に
// 完全な UserData ツリーを見せる。Web フォントは別途導入し、C++ 初期化
// 前に実体化する。内容検索、範囲キャッシュ、保存は JavaScript 側が担当する。
Module["preRun"] = Module["preRun"] || [];
Module["preRun"].push(function cardwirthPreRun() {
  "use strict";
  const dependency = "cardwirth-web-lazy-storage";
  addRunDependency(dependency);
  const status = globalThis.CardWirthWebStatus || {
    set(value) { console.log(value); },
    log(value) { console.log(value); },
    error(value) { console.error(value); }
  };
  const storage = globalThis.CardWirthWebStorage;
  if (!storage) {
    status.error("web_storage.jsを読み込めませんでした。");
    removeRunDependency(dependency);
    abort("web_storage.js was not loaded");
    return;
  }

  const CACHE_LIMIT = 32 * 1024 * 1024;
  const CHUNK_SIZE = 1024 * 1024;
  const chunkCache = new Map();
  let cachedBytes = 0;
  let persistence = Promise.resolve();
  let persistenceFailure = null;
  let persistenceState = "PersistCommitted";
  const dirtyFiles = new Set();
  // 確定済みの導入内容を MEMFS へ同期的に再適用する。その操作、
  // 特に置換するファイルノードの unlink を再び永続保存しない。
  let replayingStoredFiles = false;

  const exists = (path) => {
    try { FS.stat(path); return true; } catch (_) { return false; }
  };

  // I/O サーバービルドでは、C++ の pthread の POSIX
  // stat はすべてブラウザーメインの FS へ送られる。UserData
  // のノードは書き込み・名前変更まで不変なので、成功した stat をキャ
  // ッシュし、is_file/is_dir の反復で同じ仮想パスを走査しない
  // 。失敗結果はキャッシュせず、新規ファイルを直ちに発見できるようにする。
  const runtimeStatCache = new Map();
  const runtimeLookupCache = new Map();
  const runtimeReadDirCache = new Map();
  const runtimeAbsolutePath = (path) => {
    if (typeof path !== "string" || !path) return null;
    const normalized = PATH_FS.resolve(path.normalize("NFC").replace(/\\/g, "/"));
    return (normalized === "/user" || normalized.startsWith("/user/") || normalized.startsWith("/program/"))
      ? normalized.toLowerCase() : null;
  };
  const runtimeStatKey = (path, lstat = false) => {
    const normalized = runtimeAbsolutePath(path);
    if (!normalized) return null;
    return `${lstat ? "l:" : "s:"}${normalized}`;
  };
  const invalidateRuntimeStat = (path) => {
    const statKey = runtimeStatKey(path);
    const lstatKey = runtimeStatKey(path, true);
    if (statKey) runtimeStatCache.delete(statKey);
    if (lstatKey) runtimeStatCache.delete(lstatKey);
  };
  const clearRuntimeStatCache = () => {
    runtimeStatCache.clear();
    runtimeLookupCache.clear();
    runtimeReadDirCache.clear();
  };
  const isLookupNodeTypeError = (error) =>
    error instanceof TypeError && /node_ops/.test(String(error && error.message || error));
  const installRuntimeStatCache = () => {
    if (typeof FS === "undefined" || FS.__cardwirthRuntimeStatCacheInstalled) return;
    const originalStat = FS.stat.bind(FS);
    const originalLstat = FS.lstat.bind(FS);
    const originalLookupPath = FS.lookupPath.bind(FS);
    const originalReadDir = FS.readdir.bind(FS);
    const originalUtime = FS.utime && FS.utime.bind(FS);
    const wrap = (original, lstat) => (path) => {
      const key = runtimeStatKey(path, lstat);
      if (!key) return original(path);
      const cached = runtimeStatCache.get(key);
      if (cached) return cached;
      const value = original(path);
      runtimeStatCache.set(key, value);
      return value;
    };
    FS.stat = wrap(originalStat, false);
    FS.lstat = wrap(originalLstat, true);
    FS.lookupPath = (path, opts = {}) => {
      opts = opts || {};
      const normalized = runtimeAbsolutePath(path);
      if (!normalized) return originalLookupPath(path, opts);
      // lookupPath の結果はこれらのフラグに依存する。キー
      // を明示し、親の検索やリンク・マウント処理で非互換のノード結果
      // を共有しない。UserData に現在マウントやリンクはない
      // が、将来の取り込みにも安全に使えるようフラグの意味を保つ。
      const key = `p:${normalized}:${opts.parent ? 1 : 0}:${opts.follow ? 1 : 0}:` +
        `${opts.follow_mount === false ? 0 : 1}:${opts.noent_okay ? 1 : 0}`;
      const cached = runtimeLookupCache.get(key);
      if (cached) return cached;
      const value = originalLookupPath(path, opts);
      // lookupPath({noent_okay:true})
      // は、作成先の確認中にノードのないオブジェクト
      // を返せる。その確認結果をキャッシュせず、作成後
      // の同じパスではノードを解決する。FS.utime
      // などが未定義ノードを参照するのを防ぐ。
      if (!value || !value.node) return value;
      runtimeLookupCache.set(key, value);
      return value;
    };
    FS.readdir = (path) => {
      const normalized = runtimeAbsolutePath(path);
      if (!normalized) return originalReadDir(path);
      const key = `d:${normalized}`;
      const cached = runtimeReadDirCache.get(key);
      if (cached) return cached.slice();
      const value = originalReadDir(path);
      runtimeReadDirCache.set(key, value.slice());
      return value;
    };
    // Emscripten の IDBFS 復元はローカル項目の作
    // 成直後に FS.utime を呼ぶ。旧実行環境の古い検索結果
    // が {path} だけの確認用形式だと、分かりにくい node_ops
    // の TypeError になる。全検索キャッシュを捨て
    // て一度再試行し、作成直後のノードを現在のツリーから解決す
    // る。本当に存在しなければ通常の ENOENT を保つ。
    if (originalUtime) {
      FS.utime = (path, atime, mtime, dontFollow) => {
        try {
          const result = originalUtime(path, atime, mtime, dontFollow);
          invalidateRuntimeStat(path);
          return result;
        } catch (error) {
          if (!isLookupNodeTypeError(error)) throw error;
          clearRuntimeStatCache();
          try {
            const result = originalUtime(path, atime, mtime, dontFollow);
            invalidateRuntimeStat(path);
            return result;
          } catch (retryError) {
            if (isLookupNodeTypeError(retryError)) {
              const missing = new FS.ErrnoError(44);
              missing.path = path;
              throw missing;
            }
            throw retryError;
          }
        }
      };
    }
    Object.defineProperty(FS, "__cardwirthRuntimeStatCacheInstalled", {value: true});
  };

  const statTime = (stat) => {
    const value = stat && stat.mtime;
    if (value instanceof Date) return value.getTime();
    const number = Number(value);
    return Number.isFinite(number) ? number : Date.now();
  };

  const cacheGet = (key) => {
    const found = chunkCache.get(key);
    if (!found) return null;
    chunkCache.delete(key);
    chunkCache.set(key, found);
    return found;
  };

  const cachePut = (key, bytes) => {
    if (bytes.byteLength > CACHE_LIMIT) return bytes;
    const previous = chunkCache.get(key);
    if (previous) {
      cachedBytes -= previous.byteLength;
      chunkCache.delete(key);
    }
    chunkCache.set(key, bytes);
    cachedBytes += bytes.byteLength;
    while (cachedBytes > CACHE_LIMIT && chunkCache.size) {
      const oldestKey = chunkCache.keys().next().value;
      const oldest = chunkCache.get(oldestKey);
      chunkCache.delete(oldestKey);
      cachedBytes -= oldest.byteLength;
    }
    return bytes;
  };

  const readBlobSlice = async (blob, start, end) =>
    new Uint8Array(await blob.slice(start, end).arrayBuffer());

  class LazyFileContents {
    constructor(path, size) {
      this.path = path;
      this.length = Number(size || 0);
      this.originalLength = this.length;
      this.sourceBytes = this.length;
      this.blob = null;
      this.blobPromise = null;
    }
    async source() {
      if (this.blob) return this.blob;
      if (!this.blobPromise) {
        this.blobPromise = (this.resolveSource ? this.resolveSource() : storage.getRuntimeFile(this.path)).then((blob) => {
          if (!blob) throw new Error(`ファイル本体が見つかりません: ${this.path}`);
          // メタデータ表と元の File/Blob は一つのスナ
          // ップショット。接続フォルダーが索引作成後に変わると、
          // 古い長さで EOF を待ち続けたり置換内容を切り捨
          // てたりするため、安全側に失敗して再索引を要求する。
          if (blob.size !== this.originalLength) {
            throw new Error(
              `ファイルサイズが一覧と一致しません。再読み込みしてください: ${this.path} ` +
              `(一覧=${this.originalLength}, 実体=${blob.size})`);
          }
          this.blob = blob;
          return blob;
        });
      }
      return this.blobPromise;
    }
    async chunk(index) {
      const key = `${this.path}:${index}`;
      const cached = cacheGet(key);
      if (cached) return cached;
      const start = index * CHUNK_SIZE;
      const end = Math.min(this.originalLength, start + CHUNK_SIZE);
      const blob = await this.source();
      const bytes = await readBlobSlice(blob, start, end);
      return cachePut(key, bytes);
    }
    async copyTo(buffer, offset, length, position) {
      if (position >= this.length || length <= 0) return 0;
      const count = Math.min(length, this.length - position);
      let copied = 0;
      while (copied < count) {
        const absolute = position + copied;
        if (absolute >= this.sourceBytes) {
          buffer.fill(0, offset + copied, offset + count);
          return count;
        }
        const chunkIndex = Math.floor(absolute / CHUNK_SIZE);
        const chunkOffset = absolute % CHUNK_SIZE;
        const chunk = await this.chunk(chunkIndex);
        const part = Math.min(count - copied, chunk.byteLength - chunkOffset, this.sourceBytes - absolute);
        if (part <= 0) {
          throw new Error(`ファイル読み込みが途中で終了しました。再読み込みしてください: ${this.path}`);
        }
        buffer.set(chunk.subarray(chunkOffset, chunkOffset + part), offset + copied);
        copied += part;
      }
      return copied;
    }
    async materialize() {
      const result = new Uint8Array(this.length);
      await this.copyTo(result, 0, this.length, 0);
      return result;
    }
    snapshot() {
      // 後続の切り詰め・書き込みより前に、閉じた時点の長さを固定する。Blob の部分
      // 参照で、サイズ変更を保存するだけのために未変更ファイル全体を読むことを避ける。
      const length = this.length;
      const sourceBytes = this.sourceBytes;
      return this.source().then((blob) => new Blob([
        blob.slice(0, sourceBytes), new Uint8Array(length - sourceBytes)
      ], {type: "application/octet-stream"}));
    }
  }

  // 初期化には数千ファイルが含まれ得る。この処理中に作ったデ
  // ィレクトリーを覚え、各ファイルで mkdirTree
  // の要素走査や EEXIST の例外経路を繰り返さない。
  const createdDirectories = new Set(["/"]);
  const ensureDirectoryNormalized = (path) => {
    // 内部の呼び出し側は正規化済みの保存パスを渡す。初
    // 期化中の stat の各行で NFC・パス解析
    // を繰り返さず、末尾スラッシュの不変条件を保つ。
    if (path !== "/" && !path.endsWith("/")) path += "/";
    if (createdDirectories.has(path)) return;
    const missing = [];
    let current = path;
    while (!createdDirectories.has(current)) {
      missing.push(current);
      const slash = current.replace(/\/$/, "").lastIndexOf("/");
      current = slash <= 0 ? "/" : `${current.slice(0, slash)}/`;
    }
    for (const directory of missing.reverse()) {
      // この一覧の再適用前にパッケージ素材と保存領域が設定されるため
      // 、/program と /user は存在済みの場合がある。
      // 既存ディレクトリーは準備済みと扱い、無条件の mkdir の
      // EEXIST で Web 初期化を中断しないようにする。
      if (!exists(directory)) FS.mkdir(directory);
      else if (!FS.isDir(FS.stat(directory).mode))
        throw new Error(`ディレクトリとして使用できないパスです: ${directory}`);
      createdDirectories.add(directory);
    }
  };
  const ensureDirectory = (path) =>
    ensureDirectoryNormalized(storage.normalizePath(path, true));

  const createLazyFile = (path, size, mtimeMs, sourceBlob = null) => {
    const slash = path.lastIndexOf("/");
    ensureDirectoryNormalized(slash > 0 ? `${path.slice(0, slash)}/` : "/");
    // FS.create で空の書き込み・開く・閉じる処理を
    // 省く。直後にノードを LazyFileContents
    // に置き換えるため、初期化中の内容バッファーは不要。
    const node = FS.create(path, 0o666);
    const memoryOps = node.stream_ops;
    node.contents = new LazyFileContents(path, size);
    node.contents.resolveSource = () => {
      // ブラウザーからのアップロードも保存済みファイルと同じ遅延リーダーを使う。File 全
      // 体を ArrayBuffer にする旧方式と異なり、メタデータの準備に内容は不要。
      if (sourceBlob) return Promise.resolve(sourceBlob);
      const currentPath = FS.getPath(node);
      // 名前変更は MEMFS では同期的だが、永続保存ではキューに入る
      // 。未読み込みで名前変更したファイルだけがその完了を待ち、読み取り
      // を後の変更より先に並べる。通常の読み取りは独立した経路を保つ。
      return currentPath === path ? storage.getRuntimeFile(path)
        : enqueue(() => storage.getRuntimeFile(currentPath));
    };
    node.timestamp = Number(mtimeMs ?? Date.now());
    node.atime = node.mtime = node.ctime = node.timestamp;
    let usedBytes = size;
    let contentsGeneration = 0;
    let pendingMaterialization = null;
    Object.defineProperty(node, "usedBytes", {
      configurable: true,
      get() { return node.contents instanceof LazyFileContents ? node.contents.length : usedBytes; },
      set(value) { usedBytes = value; }
    });
    const memoryNodeOps = node.node_ops;
    node.node_ops = {...memoryNodeOps, setattr(target, attr) {
      if (attr.size !== undefined && target.contents instanceof LazyFileContents) {
        ++contentsGeneration;
        target.contents.sourceBytes = Math.min(target.contents.sourceBytes, attr.size);
        target.contents.length = attr.size;
        usedBytes = attr.size;
        // O_TRUNC で古い内容を読まない。MEMFS の書き
        // 込み・領域変更には、遅延記述子ではなく型付き配列を渡す。
        if (attr.size === 0) target.contents = new Uint8Array(0);
        const metadata = {...attr};
        delete metadata.size;
        return memoryNodeOps.setattr(target, metadata);
      }
      return memoryNodeOps.setattr(target, attr);
    }};
    const materialize = async () => {
      while (node.contents instanceof LazyFileContents) {
        const source = node.contents;
        const generation = contentsGeneration;
        // 同じ遅延世代への全書き込みが一つのスナップショットを共有する。
        // 記述子が同じでも、サイズ変更でスナップショットを無効にする。
        if (!pendingMaterialization || pendingMaterialization.source !== source ||
            pendingMaterialization.generation !== generation) {
          const pending = {source, generation};
          pending.promise = (async () => {
            try {
              const contents = await source.materialize();
              if (node.contents === source && contentsGeneration === generation) {
                node.contents = contents;
                usedBytes = contents.length;
              }
            } catch (error) {
              // 古い読み取りで O_TRUNC を取り消したり、新しい版を失敗させたりしない。
              if (node.contents === source && contentsGeneration === generation) throw error;
            }
          })();
          pendingMaterialization = pending;
        }
        const pending = pendingMaterialization;
        try { await pending.promise; }
        finally {
          if (pendingMaterialization === pending) pendingMaterialization = null;
        }
        // 待機中にサイズが変わったら、現在の世代を実体化してから MEMFS
        // の write/allocate/msync にノードを渡す。
      }
    };
    const ops = {...memoryOps};
    ops.read = (stream, buffer, offset, length, position) => {
      if (node.contents instanceof LazyFileContents)
        return node.contents.copyTo(buffer, offset, length, position);
      return memoryOps.read(stream, buffer, offset, length, position);
    };
    ops.write = (stream, buffer, offset, length, position, canOwn) => {
      if (node.contents instanceof LazyFileContents) {
        return (async () => {
          await materialize();
          return memoryOps.write(stream, buffer, offset, length, position, canOwn);
        })();
      }
      return memoryOps.write(stream, buffer, offset, length, position, canOwn);
    };
    if (memoryOps.allocate) ops.allocate = (...args) => {
      if (node.contents instanceof LazyFileContents)
        return (async () => { await materialize(); return memoryOps.allocate(...args); })();
      return memoryOps.allocate(...args);
    };
    if (memoryOps.msync) ops.msync = (...args) => {
      if (node.contents instanceof LazyFileContents)
        return (async () => { await materialize(); return memoryOps.msync(...args); })();
      return memoryOps.msync(...args);
    };
    ops.mmap = (stream, length, position) => {
      if (node.contents instanceof LazyFileContents) {
        return (async () => {
          const pointer = mmapAlloc(length);
          if (!pointer) throw new FS.ErrnoError(48);
          try {
            await node.contents.copyTo(HEAP8, pointer, length, position);
            return {ptr: pointer, allocated: true};
          } catch (error) {
            // mmapAlloc は emscripten_builtin_memalign を使
            // う。このメソッドが正常に戻った後だけ、所有権を libc の munmap に渡す。
            _emscripten_builtin_free(pointer);
            throw error;
          }
        })();
      }
      const pointer = mmapAlloc(length);
      if (!pointer) throw new FS.ErrnoError(48);
      try {
        HEAP8.set(node.contents.subarray(position, position + length), pointer);
        return {ptr: pointer, allocated: true};
      } catch (error) {
        _emscripten_builtin_free(pointer);
        throw error;
      }
    };
    node.stream_ops = ops;
  };

  const stageImportFile = (file, path) => {
    path = storage.normalizePath(path);
    if (!(file instanceof Blob) || !path.startsWith("/tmp/cardwirth-import-") ||
        exists(path)) throw new Error("アップロード用の一時ファイルを登録できません。");
    createLazyFile(path, file.size, file.lastModified, file);
  };

  const createEagerFile = async (path, mtimeMs) => {
    const slash = path.lastIndexOf("/");
    ensureDirectoryNormalized(slash > 0 ? `${path.slice(0, slash)}/` : "/");
    const blob = await storage.getRuntimeFile(path);
    if (!blob) throw new Error(`ファイル本体が見つかりません: ${path}`);
    // SDL_ttf は SDL 初期化中に OS 用フォントを開くため、その
    // パスはすでに同期的に読める必要がある。Asyncify ですべての外部フ
    // ォント処理のコールバックを安全に巻き戻せるとは限らない。固定の初期化用
    // フォントだけを実体化し、シナリオ、宿、画像、音声は遅延読み込みを保つ。
    FS.writeFile(path, new Uint8Array(await blob.arrayBuffer()));
    const node = FS.lookupPath(path).node;
    node.timestamp = node.atime = node.mtime = node.ctime = Number(mtimeMs ?? Date.now());
  };

  const removeStaleYadoLocks = (root) => {
    if (!exists(root)) return;
    for (const name of FS.readdir(root)) {
      if (name === "." || name === "..") continue;
      const path = `${root}/${name}`;
      const stat = FS.stat(path);
      if (FS.isDir(stat.mode)) removeStaleYadoLocks(path);
      else if (name === ".CardWirthPy.lock") FS.unlink(path);
    }
  };

  const enqueue = (action) => {
    persistenceState = "PersistQueued";
    const operation = persistence.then(async () => {
      persistenceState = "PersistWriting";
      return action();
    });
    // 書き込み失敗後もキューは使えるようにするが、失敗状態は保持する。
    // flushUserStorage() は再読み込みまで拒否し、IndexedDB
    // や容量超過の失敗を正常な確定応答に変えない。
    persistence = operation.then(() => {
      if (!persistenceFailure) persistenceState = "PersistCommitted";
    }, (error) => {
      if (!persistenceFailure) persistenceFailure = error;
      persistenceState = "PersistFailed";
      status.error(error && error.stack ? error.stack : String(error));
    });
    return operation;
  };

  const pendingRuntimeWrites = new Set();

  const saveDirtyFile = (path) => {
    if (!dirtyFiles.has(path) || !exists(path) || !path.startsWith("/user/")) return null;
    const stat = FS.stat(path);
    if (!FS.isFile(stat.mode)) return null;
    const node = FS.lookupPath(path).node;
    const contents = node.contents;
    // 検索成功後だけ除去する。readFile はラッパー経由でストリ
    // ームを閉じるため、スナップショット取得中は一時的にフラグを消す。
    dirtyFiles.delete(path);
    let payload;
    try {
      // 旧 FS.readFile のスナップショットと異なり、通常の MEMFS
      // のバイト列はファイル全体の中間コピーを必要としない。Blob がビュー
      // を同期的に複製するため、後の書き込みはキュー内の保存内容を変えない。共
      // 有バッファーや非バイト・独自形式の内容は FS の読み取り経路を保つ。
      const direct = ArrayBuffer.isView(contents) && contents.BYTES_PER_ELEMENT === 1 &&
        Number.isSafeInteger(stat.size) && stat.size >= 0 && stat.size <= contents.byteLength &&
        !(typeof SharedArrayBuffer !== "undefined" && contents.buffer instanceof SharedArrayBuffer);
      payload = contents instanceof LazyFileContents ? contents.snapshot()
        : new Blob([direct ? contents.subarray(0, stat.size) : FS.readFile(path)],
                   {type: "application/octet-stream"});
    } catch (error) {
      dirtyFiles.add(path);
      throw error;
    }
    // 失敗処理を直ちに登録する。未読み込みのスナップショットの読み取
    // りが失敗しても、先のファイルの確定がまだ進行中の場合がある。
    if (payload instanceof Promise) payload.catch(() => {});
    // バイトを書いてもディレクトリーの更新日時が変わるとは限らない。キュー実行時
    // ではなく、ファイルのスナップショットと同時に VFS の祖先も取得する。
    const directoryTimes = [];
    for (let parent = path.slice(0, path.lastIndexOf('/')); parent;
         parent = parent.slice(0, parent.lastIndexOf('/'))) {
      directoryTimes.push({path: parent + '/', mtimeMs: statTime(FS.stat(parent))});
    }
    const operation = enqueue(async () => storage.saveRuntimeFile(path, await payload, statTime(stat), directoryTimes));
    // 一時的な失敗も flushUserStorage() から確認できるようにし、ファイルを
    // 変更済みのまま保つ。後の明示的な同期で、少なくとも内容の再書き込みを試せるようにする。
    operation.catch(() => {
      const current = FS.getPath(node);
      if (exists(current) && FS.lookupPath(current).node === node) dirtyFiles.add(current);
    });
    return operation;
  };

  const installPersistenceTracking = () => {
    // FS は作業ディレクトリーからの相対パスを受けるが、保存キーの normalizePath("Scenario/a")
    // は cwd/Scenario/a ではなく /Scenario/a
    // を指す。非同期処理までに cwd が変わり得るため、
    // 保存をキューに入れる前に VFS のパスを解決する。
    const persistentPath = (value, directory = false) => {
      const path = String(value);
      const absolute = path.startsWith('/') ? path : `${FS.cwd()}/${path}`;
      const parts = [];
      for (const part of absolute.split('/')) {
        if (!part || part === '.') continue;
        if (part === '..') parts.pop();
        else parts.push(part);
      }
      return storage.normalizePath('/' + parts.join('/'), directory);
    };
    const streamPath = (stream) => {
      if (!stream) return null;
      if (!stream.node) return stream.path;
      const path = FS.getPath(stream.node);
      // 開いた記述子は名前変更・削除・置換後も残り得る。切り離され
      // た記述子のために、置換後の別ノードを保存してはならない。
      return exists(path) && FS.lookupPath(path).node === stream.node ? path : null;
    };
    // 最適化された Emscripten では FS.trackingDelegate が省か
    // れるため、安定した FS の API を包み、保存処理を C++ から見えない形に保つ。
    const originalMknod = FS.mknod.bind(FS);
    FS.mknod = (...args) => {
      const result = originalMknod(...args);
      // O_CREAT も FS.mkdir を呼ばずに親の一覧を変える。
      clearRuntimeStatCache();
      const path = FS.getPath(result);
      if (!replayingStoredFiles && path.startsWith("/user/") && FS.isFile(result.mode)) dirtyFiles.add(path);
      return result;
    };
    const originalWrite = FS.write.bind(FS);
    FS.write = (stream, buffer, offset, length, position, canOwn) => {
      const result = originalWrite(stream, buffer, offset, length, position, canOwn);
      const completed = (written) => {
        // 遅延書き込みは実体化を待つ場合がある。完了したバイトだけを
        // 変更済みにし、その時点のノードで名前変更・削除を解決する。
        const path = streamPath(stream);
        if (path && path.startsWith("/user/")) {
          dirtyFiles.add(path);
          invalidateRuntimeStat(path);
        }
        return written;
      };
      if (result && typeof result.then === "function") {
        const pending = Promise.resolve(result).then(completed);
        pendingRuntimeWrites.add(pending);
        pending.then(() => pendingRuntimeWrites.delete(pending),
          () => pendingRuntimeWrites.delete(pending));
        return pending;
      }
      completed(result);
      return result;
    };
    const originalClose = FS.close.bind(FS);
    FS.close = (stream) => {
      const path = streamPath(stream);
      const result = originalClose(stream);
      if (path) saveDirtyFile(path);
      return result;
    };
    const originalTruncate = FS.doTruncate.bind(FS);
    FS.doTruncate = (...args) => {
      const result = originalTruncate(...args);
      const node = args[1];
      const path = FS.getPath(node);
      if (path.startsWith("/user/")) {
        dirtyFiles.add(path);
        invalidateRuntimeStat(path);
      }
      return result;
    };
    const originalUnlink = FS.unlink.bind(FS);
    if (FS.utime) {
      const originalUtime = FS.utime.bind(FS);
      FS.utime = (path, ...times) => {
        const result = originalUtime(path, ...times);
        if (replayingStoredFiles) return result;
        const stat = FS.stat(path);
        const directory = FS.isDir(stat.mode);
        const normalized = persistentPath(path, directory);
        if (normalized.startsWith('/user/')) {
          // 新規・変更済みファイルはメタデータのトランザクションより先に行を作る。
          // 未変更ファイルは stat の保存先だけを更新し、内容を読み込まない。
          if (!directory) saveDirtyFile(normalized);
          const mtime = statTime(stat);
          enqueue(() => storage.touchRuntimePath(normalized, mtime, directory)).catch(() => {});
        }
        return result;
      };
    }
    FS.unlink = (path) => {
      const normalized = persistentPath(path);
      const result = originalUnlink(path);
      dirtyFiles.delete(normalized);
      invalidateRuntimeStat(normalized);
      clearRuntimeStatCache();
      if (!replayingStoredFiles && normalized.startsWith("/user/"))
        enqueue(() => storage.deleteRuntimePath(normalized)).catch(() => {});
      return result;
    };
    const originalRmdir = FS.rmdir.bind(FS);
    FS.rmdir = (path) => {
      const normalized = persistentPath(path);
      const result = originalRmdir(path);
      clearRuntimeStatCache();
      if (normalized.startsWith("/user/"))
        enqueue(() => storage.deleteRuntimePath(normalized)).catch(() => {});
      return result;
    };
    const originalRename = FS.rename.bind(FS);
    FS.rename = (oldPath, newPath) => {
      const oldNormalized = persistentPath(oldPath);
      const newNormalized = persistentPath(newPath);
      const result = originalRename(oldPath, newPath);
      clearRuntimeStatCache();
      if (oldNormalized !== newNormalized &&
          oldNormalized.startsWith("/user/") && newNormalized.startsWith("/user/")) {
        const within = (path, root) => path === root || path.startsWith(`${root}/`);
        const moved = Array.from(dirtyFiles).filter(path => within(path, oldNormalized));
        // 置き換えられた旧出力先は、変更済み状態の所有者ではなくなる。
        for (const path of Array.from(dirtyFiles))
          if (within(path, oldNormalized) || within(path, newNormalized)) dirtyFiles.delete(path);
        for (const path of moved) dirtyFiles.add(newNormalized + path.slice(oldNormalized.length));
        enqueue(() => storage.moveRuntimePath(oldNormalized, newNormalized)).catch(() => {});
      }
      return result;
    };
    const originalMkdir = FS.mkdir.bind(FS);
    FS.mkdir = (path, mode) => {
      const result = originalMkdir(path, mode);
      const normalized = persistentPath(path, true);
      clearRuntimeStatCache();
      if (!replayingStoredFiles && normalized.startsWith("/user/"))
        enqueue(() => storage.makeRuntimeDirectory(normalized)).catch(() => {});
      return result;
    };
  };

  const flushUserStorage = async () => {
    // 現在のキュー末尾を待つ間にも、ファイルを閉じず、保存をキ
    // ューに入れない書き込みがあり得る。保存成功を応答する前
    // に、キューと接続中の変更済みファイルの両方を処理する。
    const hasDirtyFile = () => Array.from(dirtyFiles).some(path =>
      path.startsWith("/user/") && exists(path) && FS.isFile(FS.stat(path).mode));
    let observed;
    do {
      // 未読み込みの FS.write は、変更フラグを立てる前に
      // バイトを準備中の場合がある。その失敗は書き込み側が処理し
      // 、成功した書き込みはこの同期に含めてから完了を応答する。
      await Promise.allSettled(Array.from(pendingRuntimeWrites));
      for (const path of Array.from(dirtyFiles)) saveDirtyFile(path);
      observed = persistence;
      await observed;
      // 失敗した書き込みはノードを再び変更済みにする。こ
      // の同期内で無限に再試行せず、元の失敗状態を保つ。
    } while (!persistenceFailure &&
      (pendingRuntimeWrites.size || observed !== persistence || hasDirtyFile()));
    if (persistenceFailure) {
      const detail = persistenceFailure &&
        (persistenceFailure.message || persistenceFailure.name || String(persistenceFailure));
      throw new Error(`UserDataの保存に失敗しました: ${detail}`);
    }
    persistenceState = "PersistCommitted";
  };

  const resumeAudioContexts = async () => {
    const contexts = [];
    const add = (context) => {
      if (context && typeof context.resume === "function" && !contexts.includes(context))
        contexts.push(context);
    };
    if (typeof SDL2 !== "undefined") add(SDL2.audioContext);
    // 現在の ALL/Engine の再生デバイスは SDL3 が所有する。
    // ブラウザーの利用者操作からも再開し、旧 SDL2 の文脈で代替しない。
    add(Module["SDL3"] && Module["SDL3"].audioContext);
    if (typeof AL !== "undefined" && AL.currentCtx) add(AL.currentCtx.audioCtx);
    add(Module["SDL2"] && Module["SDL2"].audioContext);
    add(Module["audioContext"]);
    for (const context of contexts) {
      if (context.state === "suspended") await context.resume();
      if (context.state === "running" && typeof context.createBufferSource === "function") {
        try {
          const source = context.createBufferSource();
          source.buffer = context.createBuffer(1, 1, context.sampleRate || 44100);
          source.connect(context.destination);
          source.onended = () => { try { source.disconnect(); } catch (_) { } };
          source.start(0);
        } catch (_) { }
      }
    }
    return contexts.length > 0 && contexts.every((context) => context.state === "running");
  };

  const refreshInstalledFiles = async (paths) => {
    if (!Array.isArray(paths)) throw new Error("インストール済みファイルの一覧が不正です。");
    const installed = new Map();
    const parents = new Set();
    for (const value of paths) {
      const path = storage.normalizePath(value);
      if (!path.startsWith("/user/") || path.endsWith("/"))
        throw new Error("インストール済みファイルのパスが不正です。");
      installed.set(path.toLowerCase(), path);
      for (let parent = path.slice(0, path.lastIndexOf("/")); parent;
           parent = parent.slice(0, parent.lastIndexOf("/"))) parents.add(parent.toLowerCase());
    }
    await flushUserStorage();
    // stat の行だけで十分。今回の Engine・Editor・Explorer
    // に見せるためだけに、ダウンロードしたファイルの内容を実体化しない。
    const stats = await storage.getRuntimeStats();
    const files = stats.filter(entry => entry.kind === "file" && installed.has(entry.path.toLowerCase()));
    if (files.length !== installed.size)
      throw new Error("インストール済みファイルの保存情報が見つかりません。");
    clearRuntimeStatCache();
    // この境界内で await しない。FS の所有スレッドで保存追跡
    // を抑制している間、無関係な実行時操作が割り込まないようにする。
    replayingStoredFiles = true;
    try {
      createdDirectories.clear();
      createdDirectories.add("/");
      for (const entry of files) {
        for (const [key, bytes] of chunkCache) {
          if (key.startsWith(`${entry.path}:`)) {
            chunkCache.delete(key);
            cachedBytes -= bytes.byteLength;
          }
        }
        if (exists(entry.path)) FS.unlink(entry.path);
        createLazyFile(entry.path, entry.size, entry.mtimeMs);
      }
      // 子の作成・削除で MEMFS の親の日時が変わる。二度目の IndexedDB
      // 変更をキューに入れず、確定済みのメタデータを復元する。
      for (const entry of stats) {
        const path = entry.path.replace(/\/$/, "");
        if (entry.kind === "directory" && parents.has(path.toLowerCase()) &&
            entry.mtimeMs != null && exists(path)) FS.utime(path, entry.mtimeMs, entry.mtimeMs);
      }
    } finally {
      replayingStoredFiles = false;
      clearRuntimeStatCache();
    }
    return files.length;
  };

  async function bootstrap() {
    const documentRef = globalThis.document;
    const fontManifestPath = Module["cardwirthFontManifestPath"] || documentRef?.querySelector(
      'meta[name="cardwirth-font-manifest"]')?.content || "";
    const requireInstalledFonts = Module["cardwirthRequireInstalledFonts"] ?? Boolean(
      documentRef?.querySelector('meta[name="cardwirth-wasm-manifest"]')?.content);
    if (!globalThis.CardWirthWebFonts ||
        typeof globalThis.CardWirthWebFonts.prepareFilesystem !== "function") {
      throw new Error("web_fonts.jsを読み込めませんでした。");
    }
    status.set("Webフォントを準備しています。");
    await globalThis.CardWirthWebFonts.prepareFilesystem(FS, fontManifestPath, {
      requireInstalled: requireInstalledFonts
    });
    status.set("Web保存DB version 1を準備しています。");
    // HTML の保存領域コントロールは、別途全体の整合性情報を要求する。Engine
    // の初期化に必要なのは DB・スキーマ移行だけなので、下で stat
    // 索引を読む前に UserData 全体を二回走査しない。
    await storage.initialize({summary: false});
    await storage.applyQueuedUserFolder();

    const mode = await storage.getStorageMode();
    if (mode === "filesystem") {
      status.set("Chromeフォルダーのメタデータを更新しています。");
      const indexed = await storage.importFileSystemDirectory(null, null, (done, total) => {
        status.set(`Chromeフォルダーを確認しています。 ${done}/${total}`);
      });
      if (!indexed.permission) throw new Error("Chromeフォルダーへ再接続してください。");
    }

    status.set("UserDataのファイル一覧を準備しています（内容は読み込みません）。");
    ensureDirectory("/user/");
    const stats = await storage.getRuntimeStats();
    // C++ のシナリオ構築は多数の小さな XML/WID を開く。旧
    // WebCardWirth の readFiles() と同様に
    // 、容量を制限した Blob レコード群を一つの IndexedDB
    // トランザクションで取得する。バイトは LazyFileContents
    // が遅延して読み、大きなメディアは先読みしない。
    const prefetchCandidates = [];
    const prefetchOptions = [];
    for (const entry of stats) {
      if (entry.kind === "directory" || !/\.(?:xml|wsm|wid|txt|json|yaml|png|bmp|jpe?g|gif|webp|apng)$/i.test(entry.path))
        continue;
      const size = Number(entry.size || 0);
      if (size < 0 || size > 512 * 1024) continue;
      const path = entry.path.toLowerCase();
      // 共通の ALL・スキン UI 画像は初期化直後に要
      // 求されるため、容量制限付きキャッシュで優先する。次
      // にシナリオ構築用の小さな XML/WID を取得し
      // 、顔画像が数千枚あっても起動時メモリーを制限する。
      const image = /\.(?:png|bmp|jpe?g|gif|webp|apng)$/i.test(path);
      const priority = path.includes("/system/") ? 0 :
        (image && path.includes("/data/skin")) ? 1 : image ? 2 : 3;
      prefetchOptions.push({path: entry.path, size, priority});
    }
    prefetchOptions.sort((left, right) => left.priority - right.priority);
    let prefetchBytes = 0;
    for (const candidate of prefetchOptions) {
      if (prefetchCandidates.length >= 2048 || prefetchBytes + candidate.size > 16 * 1024 * 1024)
        continue;
      prefetchCandidates.push(candidate);
      prefetchBytes += candidate.size;
    }
    const prefetchPromise = storage.prefetchRuntimeFiles
      ? storage.prefetchRuntimeFiles(prefetchCandidates, {
          maxFiles: 2048, maxBytes: 16 * 1024 * 1024})
      : Promise.resolve(0);
    for (const entry of stats) {
      if (entry.path === "/" || entry.path === "/user/") continue;
      if (entry.kind === "directory") ensureDirectoryNormalized(entry.path);
      else createLazyFile(entry.path, entry.size, entry.mtimeMs);
    }
    // MEMFS では子の作成で親の日時が変わる。全ツリーの作成
    // 後、保存追跡を始める前にディレクトリーのメタデータを戻す。
    for (const entry of stats) {
      if (entry.kind === 'directory' && entry.path !== '/' && entry.mtimeMs != null)
        FS.utime(entry.path, entry.mtimeMs, entry.mtimeMs);
    }
    await prefetchPromise;
    removeStaleYadoLocks("/user/Yado");
    installRuntimeStatCache();
    installPersistenceTracking();

    Module["cardwirthVirtualStorageReady"] = true;
    Module["cardwirthStageImportFile"] = stageImportFile;
    Module["cardwirthMarkDirtyFile"] = (path, node = null) => {
      if (node) {
        path = FS.getPath(node);
        if (!exists(path) || FS.lookupPath(path).node !== node) return;
      }
      if (!path || !path.startsWith("/user/")) return;
      dirtyFiles.add(path);
      invalidateRuntimeStat(path);
    };
    Module["cardwirthFlushUserStorage"] = flushUserStorage;
    Module["cardwirthRefreshInstalledFiles"] = refreshInstalledFiles;
    Module["cardwirthPersistenceState"] = () => persistenceState;
    Module["cardwirthResumeAudio"] = resumeAudioContexts;
    Module["cardwirthApplyQueuedUserUpload"] = async () => {
      await storage.applyQueuedUserFolder();
      status.log("UserDataの変更は次回起動時に反映されます。");
    };
    Module["cardwirthSyncFileSystemDirectory"] = async () => {
      const result = await storage.importFileSystemDirectory(null, null);
      clearRuntimeStatCache();
      status.log("Chromeフォルダーの変更は次回起動時に反映されます。");
      return result;
    };
    status.set("CardWirthPy_Cを初期化しています。");
  }

  bootstrap().then(() => removeRunDependency(dependency)).catch((error) => {
    const message = error && error.stack ? error.stack : String(error);
    status.error(message);
    removeRunDependency(dependency);
    abort(message);
  });
});
