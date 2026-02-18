/**
 * Aquaponics System - Shared Header Component
 * Nav-bar + Net-stats pills + Auto-active detection + Polling
 * 
 * Usage: Add to any page's <head>:
 *   <script src="/header.js"></script>
 */
(function() {
    'use strict';

    // === CSS ===
    const css = document.createElement('style');
    css.textContent = `
        /* Nav Bar */
        .nav-bar {
            display: flex; gap: 4px; padding: 8px 12px;
            background: rgba(0,0,0,0.25); border-top: 1px solid var(--glass-border, rgba(255,255,255,0.08));
            overflow-x: auto; scrollbar-width: none;
        }
        .nav-bar::-webkit-scrollbar { display: none; }
        .nav-link {
            display: flex; align-items: center; gap: 8px; padding: 10px 18px;
            border-radius: 10px; font-size: 0.85rem; font-weight: 500;
            color: var(--text-muted, #8b9bb4); text-decoration: none; cursor: pointer;
            transition: all 0.25s ease; white-space: nowrap; border: 1px solid transparent;
        }
        .nav-link:hover { background: rgba(255,255,255,0.06); color: var(--text-main, #fff); }
        .nav-link.active { background: rgba(0,242,170,0.1); color: var(--primary, #00f2aa); border-color: rgba(0,242,170,0.2); }
        .nav-link i { font-size: 0.85rem; }

        /* Net Stats Pills */
        .net-stats { display: flex; align-items: center; gap: 8px; flex-wrap: wrap; }
        .ns-item {
            display: flex; align-items: center; gap: 5px; padding: 5px 10px;
            background: rgba(255,255,255,0.03); border-radius: 16px;
            border: 1px solid var(--glass-border, rgba(255,255,255,0.08));
            font-size: 0.75rem; color: var(--text-muted, #8b9bb4);
        }
        .ns-dl { color: #10b981; }
        .ns-ul { color: #38bdf8; }
        .ns-pg { color: #fbbf24; }
        .ns-val {
            font-weight: 600; color: var(--text-main, #fff);
            font-family: 'JetBrains Mono', monospace; font-size: 0.75rem;
        }

        @media (max-width: 768px) {
            .nav-bar { padding: 6px 8px; gap: 2px; }
            .nav-link { padding: 8px 14px; font-size: 0.8rem; }
        }
    `;
    document.head.appendChild(css);

    // === Navigation links ===
    const NAV_LINKS = [
        { href: '/',          icon: 'fa-solid fa-leaf',       label: 'Dashboard' },
        { href: '/live',      icon: 'fa-solid fa-video',      label: 'Live' },
        { href: '/graphs',    icon: 'fa-solid fa-chart-line', label: 'Graphs' },
        { href: '/full_logs', icon: 'fa-solid fa-file-alt',   label: 'Logs' },
        { href: '/settings',  icon: 'fa-solid fa-cog',        label: 'Settings' },
        { href: '/ota',       icon: 'fa-solid fa-upload',     label: 'OTA' },
        { href: '/wifi',      icon: 'fa-solid fa-wifi',       label: 'WiFi' },
        { href: '/terminal',  icon: 'fa-solid fa-terminal',   label: 'Terminal' },
    ];

    // === Auto-detect active page ===
    const currentPath = window.location.pathname.replace(/\/$/, '') || '/';

    // === Build nav-bar HTML ===
    function buildNavBar() {
        const nav = document.createElement('nav');
        nav.className = 'nav-bar';
        NAV_LINKS.forEach(link => {
            const a = document.createElement('a');
            a.href = link.href;
            a.className = 'nav-link';
            const linkPath = link.href.replace(/\/$/, '') || '/';
            if (linkPath === currentPath) a.classList.add('active');
            a.innerHTML = `<i class="${link.icon}"></i> ${link.label}`;
            nav.appendChild(a);
        });
        return nav;
    }

    // === Build net-stats pills HTML ===
    function buildNetStats() {
        const div = document.createElement('div');
        div.className = 'net-stats';
        div.id = 'netStatsPills';
        div.innerHTML = `
            <div class="ns-item"><i class="fas fa-arrow-down ns-dl"></i> <span class="ns-val" id="nsDl">\u2014</span></div>
            <div class="ns-item"><i class="fas fa-arrow-up ns-ul"></i> <span class="ns-val" id="nsUl">\u2014</span></div>
            <div class="ns-item"><i class="fas fa-signal ns-pg"></i> <span class="ns-val" id="nsPing">\u2014</span></div>
        `;
        return div;
    }

    // === Inject into page ===
    function init() {
        const header = document.querySelector('.header');
        if (!header) return;

        // Insert net-stats pills into header-top (right side)
        const headerTop = header.querySelector('.header-top');
        if (headerTop) {
            headerTop.appendChild(buildNetStats());
        }

        // Insert nav-bar after header-top
        const existingNav = header.querySelector('.nav-bar');
        if (existingNav) existingNav.remove(); // Remove old nav if exists

        if (headerTop) {
            headerTop.after(buildNavBar());
        } else {
            header.appendChild(buildNavBar());
        }

        // Scroll active link into view
        const activeLink = header.querySelector('.nav-link.active');
        if (activeLink) {
            activeLink.scrollIntoView({ inline: 'nearest', block: 'nearest' });
        }

        // Start net-stats polling
        startNetStatsPoll();
    }

    // === Net-stats polling ===
    let _nsRx = null, _nsTx = null, _nsT = null;

    function formatRate(b) {
        if (b < 1024) return b.toFixed(0) + ' B/s';
        if (b < 1048576) return (b / 1024).toFixed(1) + ' KB/s';
        return (b / 1048576).toFixed(2) + ' MB/s';
    }

    async function pollNetStats() {
        try {
            const r = await fetch('/api/wifi/netstats');
            const d = await r.json();
            const n = Date.now();
            if (_nsRx !== null) {
                const t = (n - _nsT) / 1000;
                if (t > 0) {
                    const dl = document.getElementById('nsDl');
                    const ul = document.getElementById('nsUl');
                    if (dl) dl.textContent = formatRate((d.rx_bytes - _nsRx) / t);
                    if (ul) ul.textContent = formatRate((d.tx_bytes - _nsTx) / t);
                }
            }
            _nsRx = d.rx_bytes;
            _nsTx = d.tx_bytes;
            _nsT = n;
            const pg = document.getElementById('nsPing');
            if (pg) pg.textContent = d.ping_ms !== null ? d.ping_ms + ' ms' : '\u2014';
        } catch (e) { /* ignore */ }
    }

    function startNetStatsPoll() {
        pollNetStats();
        setInterval(pollNetStats, 3000);
    }

    // === Run on DOM ready ===
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }
})();
