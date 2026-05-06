/**
 * Aquaponics System - Shared Header Component
 * Nav-bar + Net-stats pills + Auto-active detection + Hamburger menu
 * 
 * Usage: Add to any page's <head>:
 *   <script src="/header.js"></script>
 */
(function() {
    'use strict';

    const SYSTEM_ALERT_REQUEST_TIMEOUT_MS = 4000;
    let _headerAlertExpanded = false;
    let _lastHeaderIssues = [];
    let _headerAlertPositionFrame = null;

    function setHeaderAlertExpanded(expanded) {
        _headerAlertExpanded = expanded;
        renderHeaderAlerts(_lastHeaderIssues);
    }

    function syncHeaderAlertDropdownPosition() {
        const pill = document.getElementById('headerAlertPill');
        const strip = document.getElementById('headerAlertStrip');
        if (!pill || !strip) {
            return;
        }

        const pillRect = pill.getBoundingClientRect();
        const viewportWidth = window.innerWidth;
        const panelWidth = Math.min(420, Math.max(280, viewportWidth - 24));
        const left = Math.min(
            Math.max(12, pillRect.right - panelWidth),
            viewportWidth - panelWidth - 12
        );

        strip.style.width = `${panelWidth}px`;
        strip.style.top = `${Math.round(pillRect.bottom + 10)}px`;
        strip.style.left = `${Math.round(left)}px`;
        strip.style.right = 'auto';
        strip.style.visibility = 'visible';
    }

    function queueHeaderAlertDropdownPositionSync() {
        const strip = document.getElementById('headerAlertStrip');
        if (!strip) {
            return;
        }

        if (_headerAlertPositionFrame !== null) {
            window.cancelAnimationFrame(_headerAlertPositionFrame);
        }

        strip.style.visibility = 'hidden';
        _headerAlertPositionFrame = window.requestAnimationFrame(() => {
            _headerAlertPositionFrame = null;
            syncHeaderAlertDropdownPosition();
        });
    }

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

        .header-alert-anchor {
            position: relative;
            display: inline-flex;
            align-items: center;
            flex: 0 0 auto;
        }

        .header-alert-pill {
            display: none;
            align-items: center;
            justify-content: center;
            width: 40px;
            height: 40px;
            padding: 0;
            border-radius: 999px;
            border: 1px solid rgba(245, 158, 11, 0.28);
            background: rgba(245, 158, 11, 0.12);
            color: #fde68a;
            white-space: nowrap;
            cursor: pointer;
            position: relative;
            appearance: none;
            transition: transform 0.18s ease, background 0.18s ease, border-color 0.18s ease;
        }

        .header-alert-pill.show {
            display: inline-flex;
        }

        .header-alert-pill:hover {
            transform: translateY(-1px);
        }

        .header-alert-pill.active {
            box-shadow: 0 0 0 1px rgba(255,255,255,0.12), 0 8px 18px rgba(0,0,0,0.18);
        }

        .header-alert-pill i {
            font-size: 0.92rem;
        }

        .header-alert-pill-badge {
            position: absolute;
            top: -4px;
            right: -4px;
            min-width: 18px;
            height: 18px;
            padding: 0 5px;
            border-radius: 999px;
            display: inline-flex;
            align-items: center;
            justify-content: center;
            background: #ef4444;
            color: #fff;
            font-size: 0.65rem;
            font-weight: 700;
            line-height: 1;
            border: 2px solid var(--bg-panel, #181b21);
        }

        .header-alert-pill-badge.hidden {
            display: none;
        }

        .header-alert-pill.critical {
            border-color: rgba(239, 68, 68, 0.36);
            background: rgba(239, 68, 68, 0.14);
            color: #fecaca;
        }

        .header-alert-pill.ok {
            border-color: rgba(16, 185, 129, 0.28);
            background: rgba(16, 185, 129, 0.12);
            color: #a7f3d0;
        }

        .header-alert-strip {
            display: none;
            position: fixed;
            top: 0;
            left: 0;
            width: 420px;
            padding: 14px 16px;
            border-radius: 16px;
            border: 1px solid rgba(245, 158, 11, 0.26);
            background: linear-gradient(135deg, rgba(245, 158, 11, 0.14), rgba(120, 53, 15, 0.12));
            color: #fef3c7;
            box-shadow: inset 0 1px 0 rgba(255,255,255,0.03), 0 18px 36px rgba(3, 8, 20, 0.24);
            z-index: 1200;
            max-height: min(70vh, 520px);
            overflow-y: auto;
            overscroll-behavior: contain;
            backdrop-filter: blur(14px);
            transform-origin: top right;
        }

        .header-alert-strip.show {
            display: block;
        }

        .header-alert-strip.critical {
            border-color: rgba(239, 68, 68, 0.28);
            background: linear-gradient(135deg, rgba(127, 29, 29, 0.24), rgba(239, 68, 68, 0.12));
            color: #fee2e2;
        }

        .header-alert-strip.ok {
            border-color: rgba(16, 185, 129, 0.24);
            background: linear-gradient(135deg, rgba(6, 78, 59, 0.24), rgba(16, 185, 129, 0.1));
            color: #d1fae5;
        }

        .header-alert-strip.compact {
            display: block;
            padding: 10px 14px;
        }

        .header-alert-strip.compact .header-alert-title {
            margin-bottom: 0;
            font-size: 0.8rem;
        }

        .header-alert-strip.compact .header-alert-summary {
            font-size: 0.74rem;
            line-height: 1.4;
        }

        .header-alert-strip.compact .header-alert-list {
            display: none;
            margin-top: 0;
        }

        .header-alert-title {
            display: flex;
            align-items: center;
            gap: 10px;
            margin-bottom: 8px;
            font-size: 0.86rem;
            font-weight: 700;
            color: inherit;
        }

        .header-alert-head {
            display: flex;
            align-items: flex-start;
            justify-content: space-between;
            gap: 12px;
            margin-bottom: 8px;
        }

        .header-alert-head .header-alert-title {
            margin-bottom: 0;
        }

        .header-alert-close {
            display: inline-flex;
            align-items: center;
            justify-content: center;
            width: 30px;
            height: 30px;
            border: 1px solid rgba(255,255,255,0.12);
            border-radius: 999px;
            background: rgba(255,255,255,0.06);
            color: inherit;
            cursor: pointer;
            appearance: none;
            transition: background 0.18s ease, border-color 0.18s ease;
        }

        .header-alert-close:hover {
            background: rgba(255,255,255,0.12);
            border-color: rgba(255,255,255,0.2);
        }

        .header-alert-summary {
            font-size: 0.78rem;
            color: inherit;
            opacity: 0.9;
            line-height: 1.55;
        }

        .header-alert-list {
            display: grid;
            gap: 8px;
            margin-top: 12px;
        }

        .header-alert-item {
            display: block;
            padding: 10px 12px;
            border-radius: 12px;
            background: rgba(0,0,0,0.16);
            border: 1px solid rgba(255,255,255,0.08);
        }

        .header-alert-item.is-link {
            color: inherit;
            text-decoration: none;
            cursor: pointer;
            transition: background 0.18s ease, border-color 0.18s ease, transform 0.18s ease;
        }

        .header-alert-item.is-link:hover {
            background: rgba(255,255,255,0.1);
            border-color: rgba(255,255,255,0.18);
            transform: translateY(-1px);
        }

        .header-alert-item-title {
            font-size: 0.78rem;
            font-weight: 700;
            color: var(--text-main, #fff);
            margin-bottom: 4px;
        }

        .header-alert-item-detail {
            font-size: 0.76rem;
            color: inherit;
            opacity: 0.92;
            line-height: 1.45;
        }

        .header-alert-item-sublist {
            display: grid;
            gap: 4px;
            margin-top: 8px;
        }

        .header-alert-item-subdetail {
            font-size: 0.74rem;
            line-height: 1.4;
            opacity: 0.88;
        }

        .header-alert-item-subdetail::before {
            content: '\\2022';
            display: inline-block;
            margin-right: 7px;
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

            .header-alert-strip {
                padding: 12px 14px;
                max-height: min(68vh, 420px);
            }

            .header-alert-strip.compact {
                padding: 10px 12px;
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

    function buildAlertPill() {
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'header-alert-pill';
        button.id = 'headerAlertPill';
        button.setAttribute('aria-label', 'Open system alerts');
        button.setAttribute('aria-expanded', 'false');
        button.innerHTML = '<i class="fas fa-bell"></i><span class="header-alert-pill-badge hidden" id="headerAlertPillBadge">0</span>';
        button.addEventListener('click', (event) => {
            event.stopPropagation();
            setHeaderAlertExpanded(!_headerAlertExpanded);
        });
        return button;
    }

    function buildAlertDropdown() {
        const anchor = document.createElement('div');
        anchor.className = 'header-alert-anchor';
        anchor.appendChild(buildAlertPill());
        return anchor;
    }

    function buildAlertStrip() {
        const div = document.createElement('div');
        div.className = 'header-alert-strip';
        div.id = 'headerAlertStrip';
        div.hidden = true;
        div.setAttribute('role', 'status');
        div.setAttribute('aria-live', 'polite');
        div.innerHTML = `
            <div class="header-alert-head">
                <div class="header-alert-title"><i class="fas fa-triangle-exclamation"></i> <span id="headerAlertTitle">System Alert</span></div>
                <button type="button" class="header-alert-close" id="headerAlertClose" aria-label="Close system alerts"><i class="fas fa-times"></i></button>
            </div>
            <div class="header-alert-summary" id="headerAlertSummary">The system reported a problem.</div>
            <div class="header-alert-list" id="headerAlertList"></div>
        `;
        const closeBtn = div.querySelector('#headerAlertClose');
        if (closeBtn) {
            closeBtn.addEventListener('click', (event) => {
                event.stopPropagation();
                setHeaderAlertExpanded(false);
            });
        }
        return div;
    }

    function renderHeaderAlerts(issues) {
        const pill = document.getElementById('headerAlertPill');
        const pillBadge = document.getElementById('headerAlertPillBadge');
        const strip = document.getElementById('headerAlertStrip');
        const title = document.getElementById('headerAlertTitle');
        const summary = document.getElementById('headerAlertSummary');
        const list = document.getElementById('headerAlertList');

        if (!pill || !pillBadge || !strip || !title || !summary || !list) {
            return;
        }

        _lastHeaderIssues = Array.isArray(issues) ? issues : [];

        strip.hidden = !_headerAlertExpanded;
        strip.setAttribute('aria-hidden', _headerAlertExpanded ? 'false' : 'true');
        if (!_headerAlertExpanded) {
            strip.style.visibility = 'hidden';
        }

        if (!_lastHeaderIssues.length) {
            pill.className = `header-alert-pill show ok ${_headerAlertExpanded ? 'active' : ''}`.trim();
            pill.setAttribute('aria-expanded', _headerAlertExpanded ? 'true' : 'false');
            pill.setAttribute('aria-label', 'Open system alerts');
            pill.title = 'System Normal';
            pillBadge.textContent = '0';
            pillBadge.className = 'header-alert-pill-badge hidden';
            strip.className = `header-alert-strip ok compact ${_headerAlertExpanded ? 'show' : ''}`.trim();
            if (_headerAlertExpanded) {
                queueHeaderAlertDropdownPositionSync();
            }
            title.textContent = 'No Active Alerts';
            summary.textContent = 'There are no current system alerts.';
            list.replaceChildren();
            return;
        }

        const hasCritical = _lastHeaderIssues.some((issue) => issue.severity === 'critical');
        pill.className = `header-alert-pill show ${hasCritical ? 'critical' : ''} ${_headerAlertExpanded ? 'active' : ''}`.trim();
        pill.setAttribute('aria-expanded', _headerAlertExpanded ? 'true' : 'false');
        pill.setAttribute('aria-label', `${_lastHeaderIssues.length} system alerts`);
        pill.title = _lastHeaderIssues.length === 1 ? '1 System Alert' : `${_lastHeaderIssues.length} System Alerts`;
        pillBadge.textContent = String(Math.min(_lastHeaderIssues.length, 99));
        pillBadge.className = 'header-alert-pill-badge';
        strip.className = `header-alert-strip ${_headerAlertExpanded ? 'show' : ''} ${hasCritical ? 'critical' : ''}`.trim();
        if (_headerAlertExpanded) {
            queueHeaderAlertDropdownPositionSync();
        }
        title.textContent = hasCritical ? 'System Problem Detected' : 'System Warning Detected';
        summary.textContent = hasCritical
            ? 'The header detected one or more active problems that need attention.'
            : 'The header detected a system warning that should be reviewed.';

        list.replaceChildren();
        _lastHeaderIssues.forEach((issue) => {
            const item = document.createElement(issue.href ? 'a' : 'div');
            item.className = `header-alert-item ${issue.href ? 'is-link' : ''}`.trim();
            if (issue.href) {
                item.href = issue.href;
            }

            const itemTitle = document.createElement('div');
            itemTitle.className = 'header-alert-item-title';
            itemTitle.textContent = issue.title;

            const itemDetail = document.createElement('div');
            itemDetail.className = 'header-alert-item-detail';
            itemDetail.textContent = issue.detail;

            item.appendChild(itemTitle);
            item.appendChild(itemDetail);

            if (Array.isArray(issue.details) && issue.details.length) {
                const sublist = document.createElement('div');
                sublist.className = 'header-alert-item-sublist';
                issue.details.forEach((detailText) => {
                    const subdetail = document.createElement('div');
                    subdetail.className = 'header-alert-item-subdetail';
                    subdetail.textContent = detailText;
                    sublist.appendChild(subdetail);
                });
                item.appendChild(sublist);
            }

            list.appendChild(item);
        });
    }

    function buildHeaderIssues(health, waterStatus, healthDetails, healthError, waterError, detailError) {
        const issues = [];

        if (healthError) {
            issues.push({
                severity: 'warn',
                title: 'Health API Unavailable',
                detail: 'The header could not refresh system health data.',
                href: '/settings'
            });
            return issues;
        }

        if (health?.esp_status !== 'ONLINE') {
            const lastSeenText = Number.isFinite(health?.last_seen_sec) && health.last_seen_sec >= 0
                ? `Last heartbeat ${health.last_seen_sec}s ago.`
                : 'No ESP32 heartbeat has been received yet.';
            issues.push({
                severity: 'critical',
                title: 'ESP32 Offline',
                detail: lastSeenText,
                href: '/hwtest'
            });
        } else if (Number.isFinite(health?.last_seen_sec) && health.last_seen_sec > 15) {
            issues.push({
                severity: 'warn',
                title: 'ESP32 Heartbeat Delayed',
                detail: `ESP32 is online but the last heartbeat is ${health.last_seen_sec}s old.`,
                href: '/hwtest'
            });
        }

        if (waterError) {
            issues.push({
                severity: 'warn',
                title: 'Water Status Unavailable',
                detail: 'The header could not refresh Water System status data.',
                href: '/hwtest'
            });
            return issues;
        }

        if (waterStatus?.alarm_active) {
            issues.push({
                severity: 'critical',
                title: 'Water Alarm Active',
                detail: waterStatus.reason || 'Water System reported an active alarm.',
                href: '/hwtest'
            });
        }

        if (detailError) {
            issues.push({
                severity: 'warn',
                title: 'Service Health Unavailable',
                detail: 'The header could not refresh service-level diagnostics.',
                href: '/settings'
            });
            return issues;
        }

        const failedServices = Array.isArray(healthDetails?.services)
            ? healthDetails.services.filter((service) => service && service.ok === false)
            : [];

        if (failedServices.length) {
            const hasCriticalServiceFailure = failedServices.some((service) => (
                service.service === 'mosquitto' || service.service === 'aquaponics-cam'
            ));
            issues.push({
                severity: hasCriticalServiceFailure ? 'critical' : 'warn',
                title: failedServices.length === 1 ? '1 Pi service failed' : `${failedServices.length} Pi services failed`,
                detail: 'Open Settings to review Pi-side services and recovery steps.',
                details: failedServices.map((service) => {
                    const displayName = service.name || service.service || 'Service';
                    const displayStatus = service.status || 'unknown';
                    return `${displayName}: ${displayStatus}`;
                }),
                href: '/settings'
            });
        }

        return issues;
    }

    function renderNavBar(header, headerTop, userRole) {
        const existingNav = header.querySelector('.nav-bar');
        if (existingNav) {
            existingNav.remove();
        }

        const navBar = buildNavBar(userRole);
        if (headerTop) {
            headerTop.after(navBar);
            return;
        }

        header.appendChild(navBar);
    }

    // === Inject into page ===
    async function init() {
        const header = document.querySelector('.header');
        if (!header) return;

        if (!document.getElementById('headerAlertStrip')) {
            document.body.appendChild(buildAlertStrip());
        }

        // Insert hamburger, net-stats pills and logout button into header-top (right side)
        const headerTop = header.querySelector('.header-top');
        if (headerTop) {
            const existingStatusBar = headerTop.querySelector('.status-bar');
            const rightGroup = existingStatusBar || document.createElement('div');
            if (!existingStatusBar) {
                rightGroup.className = 'header-right-group';
            }

            rightGroup.appendChild(buildAlertDropdown());
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

            if (!existingStatusBar) {
                headerTop.appendChild(rightGroup);
            }
        }

        renderNavBar(header, headerTop, 'user');

        // Scroll active link into view (desktop only)
        if (window.innerWidth > 768) {
            const activeLink = header.querySelector('.nav-link.active');
            if (activeLink) {
                activeLink.scrollIntoView({ inline: 'nearest', block: 'nearest' });
            }
        }

        renderHeaderAlerts([]);

        document.addEventListener('pointerdown', (event) => {
            const alertAnchor = document.querySelector('.header-alert-anchor');
            const alertStrip = document.getElementById('headerAlertStrip');
            if (
                !_headerAlertExpanded ||
                !alertAnchor ||
                alertAnchor.contains(event.target) ||
                (alertStrip && alertStrip.contains(event.target))
            ) {
                return;
            }
            setHeaderAlertExpanded(false);
        }, true);

        document.addEventListener('keydown', (event) => {
            if (event.key === 'Escape' && _headerAlertExpanded) {
                setHeaderAlertExpanded(false);
            }
        });

        window.addEventListener('resize', () => {
            if (_headerAlertExpanded) {
                queueHeaderAlertDropdownPositionSync();
            }
        });

        window.addEventListener('scroll', () => {
            if (_headerAlertExpanded) {
                queueHeaderAlertDropdownPositionSync();
            }
        }, { passive: true });

        // Start net-stats polling
        startNetStatsPoll();
        startSystemAlertPoll();

        try {
            const resp = await fetch('/api/me');
            if (!resp.ok) {
                return;
            }

            const data = await resp.json();
            const userRole = data.role || 'user';
            renderNavBar(header, headerTop, userRole);

            if (window.innerWidth > 768) {
                const activeLink = header.querySelector('.nav-link.active');
                if (activeLink) {
                    activeLink.scrollIntoView({ inline: 'nearest', block: 'nearest' });
                }
            }
        } catch(e) {}
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

    async function fetchJsonWithTimeout(url, timeoutMs = SYSTEM_ALERT_REQUEST_TIMEOUT_MS) {
        const controller = new AbortController();
        const timeoutId = window.setTimeout(() => controller.abort(), timeoutMs);

        try {
            const response = await fetch(url, {
                cache: 'no-store',
                signal: controller.signal
            });

            if (!response.ok) {
                throw new Error(url);
            }

            return await response.json();
        } finally {
            window.clearTimeout(timeoutId);
        }
    }

    async function pollSystemAlerts() {
        let health = null;
        let waterStatus = null;
        let healthDetails = null;

        const [healthResult, waterResult, detailResult] = await Promise.allSettled([
            fetchJsonWithTimeout('/api/health'),
            fetchJsonWithTimeout('/api/water_system/status'),
            fetchJsonWithTimeout('/api/health/details')
        ]);

        const healthError = healthResult.status !== 'fulfilled';
        const waterError = waterResult.status !== 'fulfilled';
        const detailError = detailResult.status !== 'fulfilled';

        if (!healthError) {
            health = healthResult.value;
        }

        if (!waterError) {
            waterStatus = waterResult.value;
        }

        if (!detailError) {
            healthDetails = detailResult.value;
        }

        renderHeaderAlerts(buildHeaderIssues(health, waterStatus, healthDetails, healthError, waterError, detailError));
    }

    function startSystemAlertPoll() {
        pollSystemAlerts();
        setInterval(pollSystemAlerts, 10000);
    }

    // === Run on DOM ready ===
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }
})();
