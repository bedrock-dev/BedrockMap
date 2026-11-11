// Supply the isolation headers required by pthreads on static hosts such as GitHub Pages.
self.addEventListener('install', event => {
    event.waitUntil(self.skipWaiting());
});

self.addEventListener('activate', event => {
    event.waitUntil(self.clients.claim());
});

self.addEventListener('fetch', event => {
    if (event.request.cache === 'only-if-cached' && event.request.mode !== 'same-origin') return;
    event.respondWith((async () => {
        const response = await fetch(event.request);
        if (response.status === 0) return response;
        const headers = new Headers(response.headers);
        headers.set('Cross-Origin-Opener-Policy', 'same-origin');
        headers.set('Cross-Origin-Embedder-Policy', 'require-corp');
        // The body is already decoded, so precompressed assets must not keep their encoding.
        headers.delete('Content-Encoding');
        headers.delete('Content-Length');
        return new Response(response.body, {
            status: response.status,
            statusText: response.statusText,
            headers
        });
    })());
});
