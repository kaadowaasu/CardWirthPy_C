"use strict";

// 旧試作の一時的なサービスワーカー経由の入口を整理する。現在の VFS
// は Blob の範囲を直接読み、ワーカーを必要としない。
self.addEventListener("install", (event) => event.waitUntil(self.skipWaiting()));
self.addEventListener("activate", (event) => event.waitUntil((async () => {
  await self.registration.unregister();
  await self.clients.claim();
})()));
