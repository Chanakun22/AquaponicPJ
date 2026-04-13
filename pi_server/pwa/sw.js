// Service Worker for Aquaponics PWA
const CACHE_NAME = 'aquaponics-v5';
const STATIC_ASSETS = [
    '/',
    '/static/fonts/fonts.css',
    '/static/fonts/outfit-300.ttf',
    '/static/fonts/outfit-400.ttf',
    '/static/fonts/outfit-500.ttf',
    '/static/fonts/outfit-600.ttf',
    '/static/fonts/outfit-700.ttf',
    '/static/fa/css/all.min.css',
    '/pwa/manifest.json',
    '/pwa/icon.svg',
    '/base.css',
    '/header.js',
    '/static/js/socket.io.min.js',
    '/static/js/chart.umd.min.js',
    '/static/js/hammer.min.js',
    '/static/js/chartjs-plugin-zoom.min.js'
];

// Install - cache static assets
self.addEventListener('install', event => {
    event.waitUntil(
        caches.open(CACHE_NAME)
            .then(cache => {
                console.log('[SW] Caching static assets');
                return cache.addAll(STATIC_ASSETS);
            })
            .catch(err => console.log('[SW] Cache failed:', err))
    );
    self.skipWaiting();
});

// Activate - clean old caches
self.addEventListener('activate', event => {
    event.waitUntil(
        caches.keys().then(keys => {
            return Promise.all(
                keys.filter(key => key !== CACHE_NAME)
                    .map(key => caches.delete(key))
            );
        })
    );
    self.clients.claim();
});

// Fetch - network first, then cache
self.addEventListener('fetch', event => {
    // Skip API calls - always fetch fresh
    if (event.request.url.includes('/api/')) {
        return;
    }
    
    event.respondWith(
        fetch(event.request)
            .then(response => {
                // Clone and cache successful responses
                if (response.status === 200) {
                    const responseClone = response.clone();
                    caches.open(CACHE_NAME)
                        .then(cache => cache.put(event.request, responseClone));
                }
                return response;
            })
            .catch(() => {
                // Fallback to cache if offline
                return caches.match(event.request);
            })
    );
});
