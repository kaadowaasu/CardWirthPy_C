// 単体版 Editor と ALL で共有する、ブラウザーの文書選択・ダウンロ
// ード連携。pthread ビルドの DOM 操作はブラウザースレッドへ非同期
// 転送する。完了状態は C++ が保持し、要求元のアプリスレッドへ結果を返す。
addToLibrary({
  cardwirth_web_open_preview_video__deps: ["$FS", "$UTF8ToString"],
  cardwirth_web_open_preview_video__proxy: "sync",
  cardwirth_web_open_preview_video: (pathPointer) => {
    try {
      Module["cardwirthPreviewVideoCleanup"]?.();
      const path = UTF8ToString(pathPointer);
      const bytes = FS.readFile(path);
      const extension = path.slice(path.lastIndexOf(".") + 1).toLowerCase();
      const mime = ({
        mp4: "video/mp4",
        m4v: "video/mp4",
        webm: "video/webm",
        ogv: "video/ogg",
        mov: "video/quicktime",
      })[extension] || "application/octet-stream";
      const url = URL.createObjectURL(new Blob([bytes], {type: mime}));
      const overlay = document.createElement("div");
      overlay.setAttribute("role", "dialog");
      overlay.setAttribute("aria-modal", "true");
      overlay.setAttribute("aria-label", "動画プレビュー");
      overlay.style.position = "fixed";
      overlay.style.inset = "0";
      overlay.style.zIndex = "2147483647";
      overlay.style.display = "grid";
      overlay.style.placeItems = "center";
      overlay.style.background = "rgba(0, 0, 0, 0.88)";
      const video = document.createElement("video");
      video.controls = true;
      video.autoplay = true;
      video.playsInline = true;
      video.tabIndex = 0;
      video.style.width = "90%";
      video.style.height = "90%";
      video.style.maxWidth = "90vw";
      video.style.maxHeight = "90vh";
      video.style.background = "black";
      const close = document.createElement("button");
      close.type = "button";
      close.textContent = "閉じる";
      close.setAttribute("aria-label", "動画プレビューを閉じる");
      close.style.position = "absolute";
      close.style.top = "max(12px, env(safe-area-inset-top))";
      close.style.right = "max(12px, env(safe-area-inset-right))";
      close.style.zIndex = "1";
      close.style.padding = "10px 18px";
      close.style.font = "600 16px sans-serif";
      close.style.border = "1px solid #fff";
      close.style.borderRadius = "6px";
      close.style.color = "#fff";
      close.style.background = "rgba(24, 24, 24, 0.9)";
      const previousFocus = document.activeElement;
      let cleaned = false;
      const keydown = (event) => {
        if (event.key !== "Escape") return;
        event.preventDefault();
        event.stopImmediatePropagation();
        Module["cardwirthPreviewVideoCleanup"]?.();
      };
      const cleanup = () => {
        if (cleaned) return;
        cleaned = true;
        document.removeEventListener("keydown", keydown, true);
        video.pause();
        video.removeAttribute("src");
        video.load();
        overlay.remove();
        URL.revokeObjectURL(url);
        if (previousFocus && typeof previousFocus.focus === "function") previousFocus.focus();
        if (Module["cardwirthPreviewVideoCleanup"] === cleanup)
          Module["cardwirthPreviewVideoCleanup"] = null;
      };
      close.addEventListener("click", cleanup);
      overlay.addEventListener("click", (event) => {
        if (event.target === overlay) cleanup();
      });
      document.addEventListener("keydown", keydown, true);
      video.addEventListener("ended", cleanup, {once: true});
      video.addEventListener("error", cleanup, {once: true});
      video.src = url;
      overlay.append(video, close);
      document.body.appendChild(overlay);
      Module["cardwirthPreviewVideoCleanup"] = cleanup;
      close.focus();
      return 1;
    } catch (error) {
      Module["printErr"]?.(`CardWirthPy_C video preview failed: ${
        error && error.message ? error.message : error}`);
      return 0;
    }
  },

  cardwirth_web_close_preview_video__proxy: "async",
  cardwirth_web_close_preview_video: () => {
    Module["cardwirthPreviewVideoCleanup"]?.();
  },

  cardwirth_web_choose_stored_document__deps: ["$FS", "$UTF8ToString", "$stringToNewUTF8", "free"],
  // C++ の呼び出し側へ戻る前に、借用した UTF-8 引数をすべて
  // 複製する。DOM 操作自体は非同期で、Wasm を一時停止しない。
  cardwirth_web_choose_stored_document__proxy: "sync",
  cardwirth_web_choose_stored_document: (requestId, mode, initial, title, extensions, multiple, confirmOverwrite, filterLabels, filterExtensions, selectedFilter) => {
    const options = {mode, initial: UTF8ToString(initial), title: UTF8ToString(title),
      extensions: UTF8ToString(extensions), multiple: !!multiple, confirmOverwrite: !!confirmOverwrite};
    const labels = filterLabels ? UTF8ToString(filterLabels).split('\n') : [];
    const patterns = filterExtensions ? UTF8ToString(filterExtensions).split('\n') : [];
    options.filters = labels.map((label, index) => ({label, extensions: patterns[index] || ''}));
    options.selectedFilter = selectedFilter >= 0 ? selectedFilter : undefined;
    const invokeText = (callback, prefix, text) => {
      const pointer = stringToNewUTF8(text);
      try { Module[callback](requestId, ...prefix, pointer); } finally { _free(pointer); }
    };
    Promise.resolve().then(() => {
      if (!globalThis.CardWirthStoragePicker) throw new Error("保存領域の選択UIが読み込まれていません");
      return globalThis.CardWirthStoragePicker.choose(FS, options);
    }).then(result => {
      const paths = result?.paths;
      if (result && Number.isInteger(result.selectedFilter))
        Module['_cardwirth_web_transfer_filter'](requestId, result.selectedFilter);
      if (paths) for (const path of paths) invokeText("_cardwirth_web_transfer_item", [], path);
      invokeText("_cardwirth_web_transfer_complete", [paths ? 0 : 1], "");
    }).catch(error => invokeText("_cardwirth_web_transfer_complete", [2], String(error.message || error)));
  },

  cardwirth_web_choose_document__deps: ["$FS", "$UTF8ToString", "$stringToNewUTF8", "free"],
  cardwirth_web_choose_document__proxy: "async",
  cardwirth_web_choose_document: (requestId, mode, suggestedNamePointer, multiple, acceptPointer, filterLabelsPointer, filterAcceptsPointer) => {
    const suggestedName = suggestedNamePointer ? UTF8ToString(suggestedNamePointer) : "";
    const accept = acceptPointer ? UTF8ToString(acceptPointer) : "";
    const filterLabels = filterLabelsPointer ? UTF8ToString(filterLabelsPointer).split("\n") : [];
    const filterAccepts = filterAcceptsPointer ? UTF8ToString(filterAcceptsPointer).split("\n") : [];
    const invoke = (name, ...values) => {
      const callback = Module[`_${name}`];
      if (typeof callback !== "function") throw new Error(`${name} is unavailable`);
      const converted = values.map((value) => {
        if (typeof value !== "string") return {value, pointer: 0};
        const pointer = stringToNewUTF8(value);
        return {value: pointer, pointer};
      });
      try {
        callback(requestId, ...converted.map((entry) => entry.value));
      } finally {
        for (const entry of converted) if (entry.pointer) _free(entry.pointer);
      }
    };
    const item = (path) => invoke("cardwirth_web_transfer_item", path);
    const done = (status, message = "") =>
      invoke("cardwirth_web_transfer_complete", status, message);
    const safePart = (value) => {
      const part = String(value || "").replace(/[\\/\0-\x1f]/g, "_");
      return !part || part === "." || part === ".." ? "unnamed" : part;
    };
    const uniquePath = (directory, relative) => {
      const parts = String(relative || "unnamed").split(/[\\/]+/).map(safePart);
      let path = `${directory}/${parts.join("/")}`;
      const dot = path.lastIndexOf(".");
      const slash = path.lastIndexOf("/");
      const stem = dot > slash ? path.slice(0, dot) : path;
      const extension = dot > slash ? path.slice(dot) : "";
      for (let suffix = 2; FS.analyzePath(path).exists; ++suffix)
        path = `${stem} (${suffix})${extension}`;
      return path;
    };
    const writeFile = async (file, path) => {
      // 全アプリ版で DataLoader のメタデータだけの File 準備を使う。こ
      // こで全内容を読むと、C++ がコピーを始める前に大きなファイルを複製してしまう。
      if (!Module["cardwirthStageImportFile"])
        throw new Error("アップロード用の読み込み機能が準備されていません。");
      Module["cardwirthStageImportFile"](file, path);
    };
    const removeTree = (entry) => {
      if (!entry || !FS.analyzePath(entry).exists) return;
      const stat = FS.stat(entry);
      if (!FS.isDir(stat.mode)) { FS.unlink(entry); return; }
      for (const name of FS.readdir(entry))
        if (name !== "." && name !== "..") removeTree(`${entry}/${name}`);
      FS.rmdir(entry);
    };
    const cleanupImportRoot = (root) => {
      try { removeTree(root); }
      catch (error) { console.error("CardWirth Web transfer: cleanup failed", error); }
    };

    const chooseWithNativeInput = () => {
      const importRoot = `/tmp/cardwirth-import-${requestId}`;
      const hasDirectoryPicker = mode === 1 &&
        typeof globalThis.showDirectoryPicker === "function";
      const overlay = document.createElement("div");
      overlay.setAttribute("role", "dialog");
      overlay.setAttribute("aria-modal", "true");
      overlay.setAttribute("aria-label", mode === 1 ? "フォルダを選択" : "ファイルを選択");
      overlay.style.position = "fixed";
      overlay.style.inset = "0";
      overlay.style.zIndex = "2147483647";
      overlay.style.display = "grid";
      overlay.style.placeItems = "center";
      overlay.style.padding = "20px";
      overlay.style.background = "rgba(0, 0, 0, 0.72)";

      const panel = document.createElement("div");
      panel.style.width = "min(360px, calc(100vw - 40px))";
      panel.style.boxSizing = "border-box";
      panel.style.padding = "20px";
      panel.style.borderRadius = "12px";
      panel.style.background = "#202124";
      panel.style.color = "#fff";
      panel.style.font = "16px/1.5 sans-serif";
      panel.style.textAlign = "center";
      panel.style.boxShadow = "0 8px 32px rgba(0, 0, 0, 0.45)";

      const message = document.createElement("div");
      message.setAttribute("aria-live", "polite");
      message.textContent = mode === 1 && hasDirectoryPicker
        ? "フォルダを選択してください。空のフォルダも保持できます。"
        : mode === 1
          ? "フォルダを選択するには、下のボタンを押してください。"
        : "ファイルを選択するには、下のボタンを押してください。";
      message.style.marginBottom = "16px";

      const directoryChoose = hasDirectoryPicker ? document.createElement("button") : null;
      if (directoryChoose) {
        // FileList/webkitdirectory は空ディレクトリーを渡せな
        // い。全ツリーを保持できるブラウザーでは、ハンドル方式の選択画面を提供する。
        directoryChoose.type = "button";
        directoryChoose.textContent = "フォルダを選択（空フォルダも保持）";
        directoryChoose.style.display = "block";
        directoryChoose.style.width = "100%";
        directoryChoose.style.minHeight = "48px";
        directoryChoose.style.padding = "10px 16px";
        directoryChoose.style.boxSizing = "border-box";
        directoryChoose.style.border = "0";
        directoryChoose.style.borderRadius = "8px";
        directoryChoose.style.color = "#202124";
        directoryChoose.style.background = "#f1f3f4";
        directoryChoose.style.font = "600 16px sans-serif";
        directoryChoose.style.cursor = "pointer";
      }

      const choose = document.createElement("div");
      choose.style.position = "relative";
      choose.style.display = "block";
      choose.style.width = "100%";
      choose.style.minHeight = "48px";
      choose.style.borderRadius = "8px";
      choose.style.overflow = "hidden";
      choose.style.background = "#f1f3f4";
      choose.style.color = "#202124";
      choose.style.cursor = "pointer";

      const chooseText = document.createElement("span");
      chooseText.textContent = mode === 1 ? "フォルダを選択" : "ファイルを選択";
      chooseText.style.display = "grid";
      chooseText.style.placeItems = "center";
      chooseText.style.minHeight = "48px";
      chooseText.style.padding = "10px 16px";
      chooseText.style.boxSizing = "border-box";
      chooseText.style.font = "600 16px sans-serif";
      chooseText.style.pointerEvents = "none";

      const input = document.createElement("input");
      input.type = "file";
      input.multiple = mode === 0 && !!multiple;
      if(mode===0 && accept)input.accept=accept;
      let filterSelect;
      if(mode===0 && filterLabels.length){
        filterSelect=document.createElement("select");filterSelect.setAttribute("aria-label","ファイルの種類");
        filterLabels.forEach((label,index)=>{const option=document.createElement("option");option.textContent=label;option.value=String(index);filterSelect.appendChild(option);});
        filterSelect.value="0";filterSelect.style.width="100%";filterSelect.style.marginBottom="12px";
        const applyFilter=()=>{const index=Number(filterSelect.value);input.accept=filterAccepts[index]??"";};
        applyFilter();filterSelect.addEventListener("change",applyFilter);
      }
      if (mode === 1) {
        input.multiple = true;
        input.webkitdirectory = true;
        input.setAttribute("directory", "");
      }
      input.setAttribute("aria-label", mode === 1 ? "フォルダを選択" : "ファイルを選択");
      input.style.position = "absolute";
      input.style.inset = "0";
      input.style.width = "100%";
      input.style.height = "100%";
      input.style.margin = "0";
      input.style.opacity = "0";
      input.style.cursor = "pointer";

      const cancel = document.createElement("button");
      cancel.type = "button";
      cancel.textContent = "キャンセル";
      cancel.style.display = "block";
      cancel.style.width = "100%";
      cancel.style.minHeight = "44px";
      cancel.style.marginTop = "10px";
      cancel.style.padding = "8px 16px";
      cancel.style.border = "1px solid #8a8a8a";
      cancel.style.borderRadius = "8px";
      cancel.style.color = "#fff";
      cancel.style.background = "transparent";
      cancel.style.font = "15px sans-serif";

      choose.append(chooseText, input);
      // フォルダーボタンは一つだけ。ブラウザーが対応すれば空フォル
      // ダーを保持するハンドル方式、未対応ならファイル入力を使う。
      if (directoryChoose) panel.append(message, directoryChoose, cancel);
      else {panel.appendChild(message);if(filterSelect)panel.appendChild(filterSelect);panel.append(choose,cancel);}
      overlay.appendChild(panel);
      document.body.appendChild(overlay);

      let settled = false;
      let reading = false;
      let returnTimer;
      let pickerPollTimer;
      let pickerWatchersInstalled = false;
      let interactionWatcherInstalled = false;

      const removePickerWatchers = () => {
        globalThis.removeEventListener("focus", afterPickerFocus);
        document.removeEventListener("visibilitychange", afterVisibility);
        document.removeEventListener("pointerdown", afterInteraction, true);
        interactionWatcherInstalled = false;
      };

      const finish = (status, messageText = "") => {
        if (settled) return;
        settled = true;
        clearTimeout(returnTimer);
        clearInterval(pickerPollTimer);
        removePickerWatchers();
        overlay.remove();
        done(status, messageText);
      };

      const importSelectedFiles = () => {
        if (settled || reading) return false;
        const files = Array.from(input.files || []);
        if (!files.length) return false;
        if (mode === 1 && files.some((file) =>
          typeof file.webkitRelativePath !== "string" || !file.webkitRelativePath)) {
          finish(2, "このブラウザはフォルダ選択に対応していません");
          return true;
        }
        reading = true;
        clearTimeout(returnTimer);
        clearInterval(pickerPollTimer);
        console.log(`CardWirth Web transfer: importing ${files.length} selected file(s)`);
        (async () => {
          try {
            FS.mkdirTree(importRoot);
            const importedPaths = [];
            if (mode === 1) {
              let top = "folder";
              for (const file of files) {
                const relative = file.webkitRelativePath;
                const parts = relative.split(/[\\/]+/).filter(Boolean);
                if (parts.length) top = safePart(parts[0]);
                await writeFile(file, uniquePath(importRoot, relative));
              }
              importedPaths.push(`${importRoot}/${top}`);
            } else {
              for (const file of files) {
                const path = uniquePath(importRoot, file.name);
                await writeFile(file, path);
                importedPaths.push(path);
              }
            }
            for (const path of importedPaths) item(path);
            console.log("CardWirth Web transfer: import completed");
            finish(0);
          } catch (error) {
            cleanupImportRoot(importRoot);
            throw error;
          }
        })().catch((error) => {
          console.error("CardWirth Web transfer: import failed", error);
          finish(2, String((error && (error.message || error.name)) || error || "browser import failed"));
        });
        return true;
      };

      const importSelectedDirectory = async () => {
        if (settled || reading) return;
        reading = true;
        clearTimeout(returnTimer);
        clearInterval(pickerPollTimer);
        try {
          const handle = await globalThis.showDirectoryPicker({mode: "read"});
          const directory = `${importRoot}/${safePart(handle.name || "folder")}`;
          FS.mkdirTree(directory);
          const importDirectory = async (source, destination) => {
            if (!source || typeof source.values !== "function")
              throw new Error("フォルダの内容を読み取れません");
            for await (const entry of source.values()) {
              const child = `${destination}/${safePart(entry.name)}`;
              if (entry.kind === "directory") {
                FS.mkdirTree(child);
                await importDirectory(entry, child);
              } else if (entry.kind === "file" && typeof entry.getFile === "function") {
                await writeFile(await entry.getFile(), child);
              } else {
                throw new Error("フォルダ内の項目を読み取れません");
              }
            }
          };
          await importDirectory(handle, directory);
          item(directory);
          finish(0);
        } catch (error) {
          cleanupImportRoot(importRoot);
          reading = false;
          if (error && error.name === "AbortError") finish(1);
          else finish(2, String((error && (error.message || error.name)) || error ||
            "フォルダの読み込みに失敗しました"));
        }
      };

      const afterPickerFocus = () => {
        clearTimeout(returnTimer);
        returnTimer = setTimeout(() => {
          if (settled || reading) return;
          // フォーカス・表示状態の復帰では、取り消しと遅い文書プロバ
          // イダーを区別できない。空選択の完了は、ネイティブの取り消
          // しイベントか表示中の Cancel ボタンだけで行う。
          if (!importSelectedFiles())
            message.textContent = "選択を確認中です。キャンセルした場合は「キャンセル」を押してください。";
        }, 500);
      };
      const afterVisibility = () => {
        if (document.visibilityState === "visible") afterPickerFocus();
      };
      const afterInteraction = (event) => {
        if (event.target !== input) afterPickerFocus();
      };
      const installPickerReturnWatchers = () => {
        if (pickerWatchersInstalled) return;
        pickerWatchersInstalled = true;
        globalThis.addEventListener("focus", afterPickerFocus);
        document.addEventListener("visibilitychange", afterVisibility);
        clearInterval(pickerPollTimer);
        pickerPollTimer = setInterval(() => {
          if (!settled && !reading) importSelectedFiles();
        }, 250);
        setTimeout(() => {
          if (settled || interactionWatcherInstalled) return;
          interactionWatcherInstalled = true;
          document.addEventListener("pointerdown", afterInteraction, true);
        }, 0);
      };

      input.addEventListener("pointerdown", installPickerReturnWatchers);
      input.addEventListener("click", installPickerReturnWatchers);
      input.addEventListener("change", importSelectedFiles);
      input.addEventListener("input", importSelectedFiles);
      input.addEventListener("cancel", () => { if (!reading) finish(1); }, {once: true});

      if (directoryChoose)
        directoryChoose.addEventListener("click", () => { void importSelectedDirectory(); });
      cancel.addEventListener("click", () => { if (!reading) finish(1); }, {once: true});
      overlay.addEventListener("click", (event) => {
        if (event.target === overlay && !reading) finish(1);
      });

      // 利用者が実際のファイル入力要素を直接操作する。pthread か
      // らメインへの転送後の合成操作は macOS/iOS の Safari
      // が拒否し得るため、input.click() は呼ばない。
      input.focus();
    };

    (async () => {
      if (mode === 4) {
        if (typeof globalThis.showDirectoryPicker !== "function")
          throw new Error("このブラウザはフォルダへの書き出しに対応していません");
        const handle = await globalThis.showDirectoryPicker({mode: "readwrite"});
        const path = `/tmp/cardwirth-export-${requestId}/folder`;
        FS.mkdirTree(path);
        const handles = Module["cardwirthWebSaveHandles"] ||
          (Module["cardwirthWebSaveHandles"] = new Map());
        handles.set(path, handle);
        item(path);
        done(0);
        return;
      }
      if (mode === 2) {
        const name = safePart(suggestedName || "download.bin");
        const path = `/tmp/cardwirth-export-${requestId}/${name}`;
        FS.mkdirTree(path.slice(0, path.lastIndexOf("/")));
        if (typeof globalThis.showSaveFilePicker === "function") {
          const handle = await globalThis.showSaveFilePicker({suggestedName: name});
          const handles = Module["cardwirthWebSaveHandles"] ||
            (Module["cardwirthWebSaveHandles"] = new Map());
          handles.set(path, handle);
        }
        item(path);
        done(0);
        return;
      }

      // すべての Web 取り込み経路で、表示された <input type=file>
      // を直接操作する。UA ごとの分岐なしで macOS Safari、iOS/iPadOS
      // Safari、デスクトップ・モバイル Chromium に対応する。
      chooseWithNativeInput();
    })().catch((error) => {
      if (error && error.name === "AbortError") done(1);
      else done(2, String((error && (error.message || error.name)) || error ||
        "browser document picker failed"));
    });
  },

  cardwirth_web_finalize_download__deps: ["$FS", "$UTF8ToString", "$stringToNewUTF8", "free"],
  cardwirth_web_finalize_download__proxy: "async",
  cardwirth_web_finalize_download: (requestId, pathPointer, success) => {
    const path = pathPointer ? UTF8ToString(pathPointer) : "";
    const invoke = (ok, message = "") => {
      const callback = Module["_cardwirth_web_download_complete"];
      if (typeof callback !== "function") return;
      const pointer = stringToNewUTF8(message);
      try { callback(requestId, ok ? 1 : 0, pointer); }
      finally { _free(pointer); }
    };
    const cleanup = () => {
      const removeTree = (entry) => {
        if (!entry || !FS.analyzePath(entry).exists) return;
        const stat = FS.stat(entry);
        if (!FS.isDir(stat.mode)) { FS.unlink(entry); return; }
        for (const name of FS.readdir(entry))
          if (name !== "." && name !== "..") removeTree(`${entry}/${name}`);
        FS.rmdir(entry);
      };
      try { removeTree(path); } catch (_) {}
      try {
        const parent = path.slice(0, path.lastIndexOf("/"));
        if (parent && FS.analyzePath(parent).exists) FS.rmdir(parent);
      } catch (_) {}
      Module["cardwirthWebSaveHandles"]?.delete(path);
    };
    (async () => {
      if (!success) {
        cleanup();
        invoke(true);
        return;
      }
      if (!path || !FS.analyzePath(path).exists) throw new Error("保存データがありません");
      const handle = Module["cardwirthWebSaveHandles"]?.get(path);
      if (handle && handle.kind === "directory") {
        const publishDirectory = async (source, destination) => {
          for (const name of FS.readdir(source)) {
            if (name === "." || name === "..") continue;
            const child = `${source}/${name}`;
            const stat = FS.stat(child);
            if (FS.isDir(stat.mode)) {
              const directory = await destination.getDirectoryHandle(name, {create: true});
              await publishDirectory(child, directory);
            } else {
              const file = await destination.getFileHandle(name, {create: true});
              const writable = await file.createWritable();
              await writable.write(FS.readFile(child));
              await writable.close();
            }
          }
        };
        await publishDirectory(path, handle);
      } else if (handle) {
        const bytes = FS.readFile(path);
        const writable = await handle.createWritable();
        await writable.write(bytes);
        await writable.close();
      } else {
        const bytes = FS.readFile(path);
        const blob = new Blob([bytes], {type: "application/octet-stream"});
        const url = URL.createObjectURL(blob);
        const anchor = document.createElement("a");
        anchor.href = url;
        anchor.download = path.slice(path.lastIndexOf("/") + 1) || "download.bin";
        anchor.style.display = "none";
        document.body.appendChild(anchor);
        anchor.click();
        anchor.remove();
        setTimeout(() => URL.revokeObjectURL(url), 0);
      }
      cleanup();
      invoke(true);
    })().catch((error) => {
      cleanup();
      invoke(false, String((error && (error.message || error.name)) || error ||
        "browser download failed"));
    });
  },
});
