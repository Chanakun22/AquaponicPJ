/**
 * Aquaponics System - Shared Header Component
 * Nav-bar + Net-stats pills + Auto-active detection + Hamburger menu
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
            display: flex; gap: 12px; padding: 10px 12px;
            background: rgba(0,0,0,0.25); border-top: 1px solid var(--glass-border, rgba(255,255,255,0.08));
            flex-wrap: wrap;
            overflow-x: auto;
            scrollbar-width: none;
        }
        .nav-bar::-webkit-scrollbar { display: none; }
        .nav-group {
            display: flex;
            align-items: center;
            gap: 10px;
            padding: 10px 12px;
            margin-right: 2px;
            border: 1px solid var(--group-border, rgba(255,255,255,0.08));
            border-radius: 14px;
            background: var(--group-bg, rgba(255,255,255,0.03));
            box-shadow: inset 0 1px 0 rgba(255,255,255,0.04);
        }
        .nav-group:last-child {
            margin-right: 0;
        }
        .nav-group[data-section="monitor"] {
            --group-accent: #38bdf8;
            --group-bg: rgba(56,189,248,0.08);
            --group-border: rgba(56,189,248,0.18);
        }
        .nav-group[data-section="operate"] {
            --group-accent: #34d399;
            --group-bg: rgba(52,211,153,0.08);
            --group-border: rgba(52,211,153,0.18);
        }
        .nav-group[data-section="admin"] {
            --group-accent: #f59e0b;
            --group-bg: rgba(245,158,11,0.08);
            --group-border: rgba(245,158,11,0.18);
        }
        .nav-group-label {
            display: inline-flex;
            align-items: center;
            gap: 6px;
            padding: 8px 10px;
            color: var(--group-accent, var(--text-muted, #8b9bb4));
            font-size: 0.72rem;
            font-weight: 700;
            letter-spacing: 0.08em;
            text-transform: uppercase;
            white-space: nowrap;
            border-radius: 10px;
            background: rgba(0,0,0,0.16);
            border: 1px solid rgba(255,255,255,0.06);
            min-height: 40px;
        }
        .nav-group-links {
            display: flex;
            flex-wrap: wrap;
            gap: 4px;
        }
        .nav-link {
            display: flex; align-items: center; gap: 8px; padding: 10px 18px;
            border-radius: 10px; font-size: 0.85rem; font-weight: 500;
            color: var(--text-muted, #8b9bb4); text-decoration: none; cursor: pointer;
            transition: all 0.25s ease; white-space: nowrap; border: 1px solid transparent;
            background: rgba(255,255,255,0.02);
        }
        .nav-link:hover { background: rgba(255,255,255,0.08); color: var(--text-main, #fff); }
        .nav-link.active {
            background: color-mix(in srgb, var(--group-accent, var(--primary, #00f2aa)) 14%, transparent);
            color: var(--group-accent, var(--primary, #00f2aa));
            border-color: color-mix(in srgb, var(--group-accent, var(--primary, #00f2aa)) 28%, transparent);
            box-shadow: inset 0 0 0 1px color-mix(in srgb, var(--group-accent, var(--primary, #00f2aa)) 12%, transparent);
        }
        .nav-link:focus-visible,
        .btn-logout:focus-visible,
        .hamburger-btn:focus-visible {
            outline: 2px solid var(--secondary, #38bdf8);
            outline-offset: 2px;
        }
        .nav-link i { font-size: 0.85rem; }

        /* Logout Button */
        .btn-logout {
            display: flex; align-items: center; gap: 6px; padding: 7px 14px;
            border-radius: 10px; font-size: 0.8rem; font-weight: 500;
            color: #ff6b8a; background: rgba(255,42,109,0.08);
            border: 1px solid rgba(255,42,109,0.2); cursor: pointer;
            font-family: inherit; transition: all 0.25s ease; white-space: nowrap;
        }
        .btn-logout:hover { background: rgba(255,42,109,0.15); border-color: rgba(255,42,109,0.4); }
        .btn-logout i { font-size: 0.8rem; }

        /* Net Stats Pills */
        .net-stats { display: flex; align-items: center; gap: 8px; flex-wrap: wrap; }
        .ns-item {
            display: flex; align-items: center; gap: 5px; padding: 5px 10px;
            background: rgba(255,255,255,0.03); border-radius: 16px;
            border: 1px solid var(--glass-border, rgba(255,255,255,0.08));
            font-size: 0.75rem; color: var(--text-muted, #8b9bb4);
            cursor: help;
        }
        .ns-dl { color: #10b981; }
        .ns-ul { color: #38bdf8; }
        .ns-pg { color: #fbbf24; }
        .ns-val {
            font-weight: 600; color: var(--text-main, #fff);
            font-family: 'JetBrains Mono', monospace; font-size: 0.75rem;
        }

        /* Header Right Group */
        .header-right-group {
            display: flex; align-items: center; gap: 10px; flex-wrap: wrap;
        }

        /* Hamburger Button */
        .hamburger-btn {
            display: none; /* Hidden on desktop */
            align-items: center; justify-content: center;
            width: 40px; height: 40px;
            border-radius: 10px; border: 1px solid var(--glass-border, rgba(255,255,255,0.08));
            background: rgba(255,255,255,0.06); color: var(--text-main, #fff);
            cursor: pointer; font-size: 1.2rem;
            transition: all 0.25s ease;
        }
        .hamburger-btn:hover { background: rgba(255,255,255,0.12); }
        .hamburger-btn.open { background: rgba(0,242,170,0.1); color: var(--primary, #00f2aa); }

        /* ===== MOBILE RESPONSIVE ===== */
        @media (max-width: 768px) {
            /* Show hamburger */
            .hamburger-btn { display: flex; }

            /* Nav bar: collapsible */
            .nav-bar {
                display: none; /* Hidden by default on mobile */
                flex-direction: column;
                padding: 8px;
                gap: 8px;
            }
            .nav-group {
                width: 100%;
                flex-direction: column;
                align-items: stretch;
                gap: 6px;
                padding: 10px;
                margin: 0;
                border-bottom: 1px solid var(--group-border, rgba(255,255,255,0.08));
            }
            .nav-group:last-child {
                border-bottom: none;
            }
            .nav-group-label {
                padding: 10px 12px;
            }
            .nav-group-links {
                flex-direction: column;
                gap: 4px;
            }
            .nav-bar.open { display: flex; }
            .nav-link {
                padding: 12px 16px; font-size: 0.9rem;
                border-radius: 8px; width: 100%;
            }

            /* Header top: stack on very small */
            .header-right-group {
                gap: 6px;
            }

            /* Net stats: smaller */
            .ns-item { padding: 3px 7px; font-size: 0.7rem; }
            .ns-val { font-size: 0.7rem; }

            /* Logout: compact */
            .btn-logout { padding: 5px 10px; font-size: 0.75rem; }
        }

        @media (max-width: 480px) {
            .net-stats { display: none; } /* Hide net stats on very small screens */
            .header-right-group { gap: 6px; }
        }
    `;
    document.head.appendChild(css);

    // === Navigation links ===
    const NAV_SECTIONS = [
        {
            key: 'monitor',
            label: 'ติดตาม',
            icon: 'fa-solid fa-chart-simple',
            links: [
                { href: '/',          icon: 'fa-solid fa-leaf',       label: 'ภาพรวมระบบ' },
                { href: '/live',      icon: 'fa-solid fa-video',      label: 'กล้องสด' },
                { href: '/graphs',    icon: 'fa-solid fa-chart-line', label: 'ข้อมูลย้อนหลัง' },
                { href: '/full_logs', icon: 'fa-solid fa-file-alt',   label: 'บันทึกระบบ' }
            ]
        },
        {
            key: 'operate',
            label: 'สั่งงาน',
            icon: 'fa-solid fa-sliders',
            links: [
                { href: '/hwtest',   icon: 'fa-solid fa-vial',     label: 'ทดสอบฮาร์ดแวร์', admin: true },
                { href: '/settings', icon: 'fa-solid fa-cog',      label: 'ตั้งค่า', admin: true },
                { href: '/ota',      icon: 'fa-solid fa-upload',   label: 'OTA', admin: true },
                { href: '/wifi',     icon: 'fa-solid fa-wifi',     label: 'WiFi', admin: true },
                { href: '/terminal', icon: 'fa-solid fa-terminal', label: 'Terminal', admin: true }
            ]
        },
        {
            key: 'admin',
            label: 'ผู้ดูแล',
            icon: 'fa-solid fa-shield-halved',
            links: [
                { href: '/admin/logs',  icon: 'fa-solid fa-clipboard-list', label: 'กิจกรรม' },
                { href: '/admin/users', icon: 'fa-solid fa-users-cog',      label: 'ผู้ใช้', admin: true }
            ]
        }
    ];

    // === Auto-detect active page ===
    const currentPath = window.location.pathname.replace(/\/$/, '') || '/';

    // === Build nav-bar HTML (filtered by role) ===
    function buildNavBar(userRole) {
        const nav = document.createElement('nav');
        nav.className = 'nav-bar';
        nav.id = 'mainNavBar';
        NAV_SECTIONS.forEach(section => {
            const visibleLinks = section.links.filter(link => !link.admin || userRole === 'admin');
            if (visibleLinks.length === 0) {
                return;
            }

            const group = document.createElement('div');
            group.className = 'nav-group';
            group.dataset.section = section.key || 'default';

            const label = document.createElement('div');
            label.className = 'nav-group-label';
            label.innerHTML = `<i class="${section.icon}"></i> ${section.label}`;
            group.appendChild(label);

            const linksWrap = document.createElement('div');
            linksWrap.className = 'nav-group-links';

            visibleLinks.forEach(link => {
                const a = document.createElement('a');
                a.href = link.href;
                a.className = 'nav-link';
                const linkPath = link.href.replace(/\/$/, '') || '/';
                if (linkPath === currentPath) a.classList.add('active');
                a.innerHTML = `<i class="${link.icon}"></i> ${link.label}`;
                linksWrap.appendChild(a);
            });

            group.appendChild(linksWrap);
            nav.appendChild(group);
        });
        return nav;
    }

    // === Build hamburger button ===
    function buildHamburger() {
        const btn = document.createElement('button');
        btn.className = 'hamburger-btn';
        btn.id = 'hamburgerBtn';
        btn.innerHTML = '<i class="fas fa-bars"></i>';
        btn.setAttribute('aria-label', 'เปิดหรือปิดเมนูนำทาง');
        btn.onclick = function() {
            const nav = document.getElementById('mainNavBar');
            if (nav) {
                nav.classList.toggle('open');
                btn.classList.toggle('open');
                btn.innerHTML = nav.classList.contains('open')
                    ? '<i class="fas fa-times"></i>'
                    : '<i class="fas fa-bars"></i>';
            }
        };
        return btn;
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
    async function init() {
        const header = document.querySelector('.header');
        if (!header) return;

        // Fetch user role
        let userRole = 'user';
        try {
            const resp = await fetch('/api/me');
            if (resp.ok) {
                const data = await resp.json();
                userRole = data.role || 'user';
            }
        } catch(e) {}

        // Insert hamburger, net-stats pills and logout button into header-top (right side)
        const headerTop = header.querySelector('.header-top');
        if (headerTop) {
            const rightGroup = document.createElement('div');
            rightGroup.className = 'header-right-group';
            rightGroup.appendChild(buildNetStats());

            // Logout button
            const logoutBtn = document.createElement('button');
            logoutBtn.className = 'btn-logout';
            logoutBtn.innerHTML = '<i class="fas fa-sign-out-alt"></i> ออกจากระบบ';
            logoutBtn.onclick = async function() {
                try {
                    await fetch('/api/logout', { method: 'POST' });
                } catch(e) {}
                window.location.href = '/login';
            };
            rightGroup.appendChild(logoutBtn);

            // Hamburger button (only visible on mobile via CSS)
            rightGroup.appendChild(buildHamburger());

            headerTop.appendChild(rightGroup);
        }

        // Insert nav-bar after header-top
        const existingNav = header.querySelector('.nav-bar');
        if (existingNav) existingNav.remove();

        if (headerTop) {
            headerTop.after(buildNavBar(userRole));
        } else {
            header.appendChild(buildNavBar(userRole));
        }

        // Scroll active link into view (desktop only)
        if (window.innerWidth > 768) {
            const activeLink = header.querySelector('.nav-link.active');
            if (activeLink) {
                activeLink.scrollIntoView({ inline: 'nearest', block: 'nearest' });
            }
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
            if (pg) {
                pg.textContent = d.ping_ms !== null ? d.ping_ms + ' ms' : '\u2014';
                if (d.ping_target) {
                    const title = 'Ping ไปยัง: ' + d.ping_target;
                    pg.parentElement.title = title;
                    pg.title = title;
                }
            }
        } catch (e) { /* silent */ }
    }

    function startNetStatsPoll() {
        pollNetStats();
        setInterval(pollNetStats, 10000);
    }

    // === Run on DOM ready ===
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }
})();
