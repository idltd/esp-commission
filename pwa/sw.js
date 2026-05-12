const CACHE = 'device-setup-v9';
const ASSETS = [
  './',
  './index.html',
  './manifest.json',
  './icon.svg',
  './decoder.js',
  './app.js'
];

self.addEventListener('install', e => {
  e.waitUntil(caches.open(CACHE).then(c => c.addAll(ASSETS)));
  self.skipWaiting();
});

self.addEventListener('activate', e =>
  e.waitUntil(
    caches.keys()
      .then(keys => Promise.all(keys.filter(k => k !== CACHE).map(k => caches.delete(k))))
      .then(() => self.clients.claim())
  )
);

self.addEventListener('fetch', e => {
  if (e.request.url.includes('192.168.4.1')) return;
  e.respondWith(
    caches.match(e.request).then(r => r || fetch(e.request))
  );
});
