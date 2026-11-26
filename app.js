const tabs = [...document.querySelectorAll('[role="tab"]')];
function selectPlatform(platform, focus = false) {
  for (const tab of tabs) {
    const selected = tab.dataset.platform === platform;
    tab.setAttribute('aria-selected', String(selected));
    tab.tabIndex = selected ? 0 : -1;
    document.getElementById(tab.getAttribute('aria-controls')).hidden = !selected;
    if (selected && focus) tab.focus();
  }
}
for (const tab of tabs) {
  tab.addEventListener('click', () => selectPlatform(tab.dataset.platform));
  tab.addEventListener('keydown', e => {
    if (!['ArrowLeft','ArrowRight','Home','End'].includes(e.key)) return;
    e.preventDefault();
    const index = tabs.indexOf(tab);
    const next = e.key === 'Home' ? 0 : e.key === 'End' ? tabs.length - 1 : (index + (e.key === 'ArrowRight' ? 1 : -1) + tabs.length) % tabs.length;
    selectPlatform(tabs[next].dataset.platform, true);
  });
}
for (const link of document.querySelectorAll('[data-select]')) {
  link.addEventListener('click', () => selectPlatform(link.dataset.select));
}
const toast = document.getElementById('toast');
let toastTimeout;
function showToast(message) {
  toast.textContent = message;
  toast.classList.add('show');
  clearTimeout(toastTimeout);
  toastTimeout = setTimeout(() => toast.classList.remove('show'), 2600);
}
for (const button of document.querySelectorAll('[data-copy]')) {
  button.addEventListener('click', async () => {
    const code = button.parentElement.querySelector('code').innerText;
    try {
      await navigator.clipboard.writeText(code);
      showToast('Команда скопирована');
    } catch {
      const area = document.createElement('textarea');
      area.value = code; area.style.position = 'fixed'; area.style.top = '-999px';
      document.body.appendChild(area); area.select();
      let ok = false;
      try { ok = document.execCommand('copy'); } catch {}
      area.remove(); button.focus();
      showToast(ok ? 'Команда скопирована' : 'Выдели и скопируй команду вручную');
    }
  });
}
