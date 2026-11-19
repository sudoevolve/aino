/* Wiki 正文与导航均来自 docs/wiki；此文件只处理读取、渲染和浏览器导航。 */
'use strict';
(() => {
  const $ = id => document.getElementById(id);
  const config = document.querySelector('meta[name="aino-repository-base"]').content;
  const repo = new URL(config, location.href);
  const ref = document.querySelector('meta[name="aino-repository-ref"]').content;
  const github = `https://github.com/sudoevolve/aino/blob/${encodeURIComponent(ref)}/`;
  const wikiBase = new URL('docs/wiki/', repo);
  const cache = new Map();
  let chapters = [], controller, generation = 0;
  const escapeHtml = text => text.replace(/[&<>"']/g, ch => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[ch]));
  const route = (file, hash = '') => {
    const url = new URL(location.href);
    url.search = new URLSearchParams({page: file}).toString();
    url.hash = hash;
    return url.pathname + url.search + url.hash;
  };
  const link = (label, href) => {
    const a = document.createElement('a'); a.textContent = label; a.href = href; return a;
  };
  const setTheme = value => {
    document.documentElement.dataset.theme = value;
    $('theme').textContent = value === 'dark' ? '浅色' : '深色';
    $('theme').setAttribute('aria-label', value === 'dark' ? '切换为浅色主题' : '切换为深色主题');
  };
  let storedTheme;
  try { storedTheme = localStorage.getItem('aino-theme'); } catch (_) { /* 隐私模式仍可切换本页 */ }
  const systemTheme = matchMedia('(prefers-color-scheme: dark)');
  setTheme(['dark', 'light'].includes(storedTheme) ? storedTheme : systemTheme.matches ? 'dark' : 'light');
  systemTheme.addEventListener('change', event => { if (!storedTheme) setTheme(event.matches ? 'dark' : 'light'); });
  $('theme').addEventListener('click', () => {
    storedTheme = document.documentElement.dataset.theme === 'dark' ? 'light' : 'dark';
    setTheme(storedTheme);
    try { localStorage.setItem('aino-theme', storedTheme); } catch (_) { /* 页面内选择仍有效 */ }
  });
  document.querySelectorAll('[data-repo-image]').forEach(img => { img.src = new URL(img.dataset.repoImage, repo).href; });
  document.querySelectorAll('[data-repo-link]').forEach(a => { a.href = new URL(a.dataset.repoLink, repo).href; });
  $('menu').addEventListener('click', () => {
    const open = $('sidebar').classList.toggle('open');
    $('menu').setAttribute('aria-expanded', String(open));
  });
  $('search').addEventListener('input', () => {
    const query = $('search').value.trim().toLocaleLowerCase();
    let visible = 0;
    $('chapters').querySelectorAll('a').forEach(a => {
      a.hidden = !a.textContent.toLocaleLowerCase().includes(query);
      if (!a.hidden) visible++;
    });
    $('no-chapters').hidden = visible > 0;
  });

  async function read(file, signal) {
    if (cache.has(file)) return cache.get(file);
    const response = await fetch(new URL(file, wikiBase), {signal, cache: 'no-cache'});
    if (!response.ok) throw new Error(`无法读取教程（HTTP ${response.status}）`);
    const source = await response.text();
    if (/^\s*<!doctype html|^\s*<html[\s>]/i.test(source)) throw new Error('教程地址返回了 HTML，请检查文档部署路径');
    cache.set(file, source);
    return source;
  }

  function markdown(source) {
    // 禁用原始 HTML，并在插入 DOM 前使用成熟的清洗器限制标签、属性与 URL 协议。
    const parser = new marked.Marked({gfm: true, breaks: false, renderer: {html: token => escapeHtml(token.text)}});
    return DOMPurify.sanitize(parser.parse(source), {
      ALLOWED_TAGS: ['h1','h2','h3','h4','h5','h6','p','a','img','pre','code','blockquote','ul','ol','li','strong','em','del','hr','br','table','thead','tbody','tr','th','td','input'],
      ALLOWED_ATTR: ['href','src','alt','title','class','align','start','type','checked','disabled'],
      ALLOW_DATA_ATTR: false,
    });
  }

  async function loadChapters(signal) {
    if (chapters.length) return;
    const root = document.createElement('div');
    root.innerHTML = markdown(await read('README.md', signal));
    chapters = [{file: 'README.md', title: '教程总览'}];
    root.querySelectorAll('table a[href]').forEach(a => {
      const file = a.getAttribute('href');
      if (/^\d{2}-[a-z0-9-]+\.md$/.test(file) && !chapters.some(c => c.file === file))
        chapters.push({file, title: a.textContent});
    });
    if (chapters.length === 1) { chapters = []; throw new Error('教程目录为空，请检查 docs/wiki/README.md'); }
    $('chapters').replaceChildren(...chapters.map(c => link(c.title, route(c.file))));
  }

  function rewriteReferences(article, file) {
    const documentUrl = new URL(file, wikiBase);
    article.querySelectorAll('a[href], img[src]').forEach(element => {
      const image = element.tagName === 'IMG';
      const attribute = image ? 'src' : 'href';
      const value = element.getAttribute(attribute);
      try {
        const url = new URL(value, documentUrl);
        if (!['https:', 'http:', ...(image ? [] : ['mailto:'])].includes(url.protocol)) {
          element.removeAttribute(attribute); return;
        }
        if (value.startsWith('#') && !image) element.href = route(file, url.hash);
        else if (url.origin === wikiBase.origin && url.pathname.startsWith(wikiBase.pathname)) {
          const target = decodeURIComponent(url.pathname.slice(wikiBase.pathname.length));
          if (!image && chapters.some(c => c.file === target)) element.href = route(target, url.hash);
          else element.setAttribute(attribute, url.href);
        } else if (url.origin === repo.origin && url.pathname.startsWith(repo.pathname)) {
          const relative = url.pathname.slice(repo.pathname.length);
          element.setAttribute(attribute, image ? url.href : github + relative + url.hash);
        } else element.setAttribute(attribute, url.href);
        if (image) { element.loading = 'lazy'; element.decoding = 'async'; }
        else if (new URL(element.href, location.href).origin !== location.origin) element.rel = 'noopener noreferrer';
      } catch (_) { element.removeAttribute(attribute); }
    });
    article.querySelectorAll('table').forEach(table => {
      const wrapper = document.createElement('div'); wrapper.className = 'table-scroll';
      wrapper.tabIndex = 0; wrapper.setAttribute('role', 'region'); wrapper.setAttribute('aria-label', '可横向滚动的数据表');
      table.replaceWith(wrapper); wrapper.append(table);
    });
  }

  function headings(article, file) {
    const used = new Set(), outline = document.createDocumentFragment();
    const title = document.createElement('strong'); title.textContent = '本页目录'; outline.append(title);
    article.querySelectorAll('h1,h2,h3,h4,h5,h6').forEach(heading => {
      const base = heading.textContent.trim().toLowerCase().replace(/[^\p{L}\p{M}\p{N}_\-\s]/gu, '').replace(/\s/g, '-');
      let id = base, index = 0;
      while (used.has(id)) id = `${base}-${++index}`;
      used.add(id); heading.id = id;
      if (['H2', 'H3'].includes(heading.tagName)) {
        const a = link(heading.textContent, route(file, id));
        if (heading.tagName === 'H3') a.className = 'sub';
        outline.append(a);
      }
    });
    $('outline').replaceChildren(outline);
  }

  function scrollToAnchor() {
    if (!location.hash) { window.scrollTo(0, 0); return; }
    try {
      const heading = document.getElementById(decodeURIComponent(location.hash.slice(1)));
      const active = $('home').hidden ? $('content') : $('home');
      if (heading && active.contains(heading)) heading.scrollIntoView();
    } catch (_) { /* 不合法的 URL fragment 不影响文档读取 */ }
  }

  async function render() {
    const current = ++generation;
    if (controller) controller.abort();
    controller = new AbortController();
    const {signal} = controller;
    const file = new URLSearchParams(location.search).get('page');
    document.querySelectorAll('[data-nav-home], [data-nav-start], [data-nav-wiki]').forEach(a => {
      const selected = a.hasAttribute('data-nav-home') ? file === null
        : a.hasAttribute('data-nav-start') ? file === '01-quickstart.md'
        : file !== null && file !== '01-quickstart.md';
      if (selected) a.setAttribute('aria-current', 'page');
      else a.removeAttribute('aria-current');
    });
    $('home').hidden = file !== null; $('wiki').hidden = file === null;
    $('sidebar').classList.remove('open'); $('menu').setAttribute('aria-expanded', 'false');
    if (file === null) { document.title = 'aino · C++ 桌面 UI'; scrollToAnchor(); return; }
    $('content').replaceChildren(); $('pager').replaceChildren(); $('outline').replaceChildren();
    $('status').textContent = '正在读取仓库教程…'; $('retry').hidden = true;
    $('content').setAttribute('aria-busy', 'true');
    const requestController = controller;
    const timeout = setTimeout(() => requestController.abort(), 12000);
    try {
      if (!window.marked || !window.DOMPurify) throw new Error('Markdown 阅读组件未加载，请刷新页面');
      await loadChapters(signal);
      if (current !== generation) return;
      const index = chapters.findIndex(c => c.file === file);
      if (index < 0) throw new Error('未找到这篇教程，请从左侧目录选择');
      const source = await read(file, signal);
      if (current !== generation) return;
      $('content').innerHTML = markdown(source);
      rewriteReferences($('content'), file); headings($('content'), file);
      document.title = `${chapters[index].title} · aino Wiki`;
      $('doc-location').textContent = `Wiki / ${chapters[index].title}`;
      $('source').href = github + 'docs/wiki/' + encodeURIComponent(file);
      $('chapters').querySelectorAll('a').forEach(a => {
        if (new URL(a.href).searchParams.get('page') === file) a.setAttribute('aria-current', 'page');
        else a.removeAttribute('aria-current');
      });
      if (index > 0) $('pager').append(link(`← ${chapters[index - 1].title}`, route(chapters[index - 1].file)));
      if (index + 1 < chapters.length) $('pager').append(link(`${chapters[index + 1].title} →`, route(chapters[index + 1].file)));
      $('status').textContent = '';
      scrollToAnchor();
    } catch (error) {
      if (current !== generation) return;
      $('status').textContent = signal.aborted ? '读取超时，请检查网络后重试。' : `${error.message}。`;
      $('retry').hidden = false;
      document.title = '教程读取失败 · aino Wiki';
    } finally {
      clearTimeout(timeout);
      if (current === generation) $('content').setAttribute('aria-busy', 'false');
    }
  }

  document.addEventListener('click', event => {
    const a = event.target.closest('a[href]');
    if (!a || event.defaultPrevented || event.button !== 0 || event.ctrlKey || event.metaKey || event.shiftKey || event.altKey || a.target || a.hasAttribute('download')) return;
    const target = new URL(a.href);
    if (target.origin !== location.origin || target.pathname !== location.pathname) return;
    event.preventDefault(); history.pushState(null, '', target); render();
  });
  $('retry').addEventListener('click', () => render());
  window.addEventListener('popstate', () => render());
  render();
})();
