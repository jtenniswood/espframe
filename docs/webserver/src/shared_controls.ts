// Shared controls: SELECT
export function selectFromOptions(options, current, onChange, optionDisplayFn?) {
  var display = optionDisplayFn || function (o) { return o; };
  var sel = document.createElement("select");
  sel.className = "select";
  options.forEach(function (o) {
    var opt = document.createElement("option");
    opt.value = o;
    opt.textContent = display(o);
    if (o === current) opt.selected = true;
    sel.appendChild(opt);
  });
  sel.onchange = function () {
    onChange(sel.value);
  };
  return sel;
}

// Shared controls: ELEMENT
export function el<K extends keyof HTMLElementTagNameMap>(tag: K, cls?: string): HTMLElementTagNameMap[K] {
  var e = document.createElement(tag);
  if (cls) e.className = cls;
  return e;
}

// Shared controls: ACTIONS
export function setStatus(target, msg, type?, clearAfterMs?) {
  if (!target) return;
  target.replaceChildren();
  if (!msg) {
    target.textContent = "";
    return;
  }
  var dot = el("span", "dot " + (type || "green"));
  target.appendChild(dot);
  target.appendChild(document.createTextNode(" " + msg));
  clearTimeout(target._t);
  if (clearAfterMs) {
    target._t = setTimeout(function () {
      target.textContent = "";
    }, clearAfterMs);
  }
}

export function button(text, cls?, onClick?) {
  var btn = el("button", cls || "btn btn-secondary");
  btn.type = "button";
  btn.textContent = text;
  if (onClick) btn.onclick = onClick;
  return btn;
}

// Shared controls: CARDS
var controlId = 0;

export function bindDisclosure(toggle: HTMLButtonElement, panel: HTMLElement, body: HTMLElement,
                        className: string, expandedWhenPresent: boolean) {
  if (!body.id) body.id = "disclosure-" + (++controlId);
  toggle.setAttribute("aria-controls", body.id);
  function syncExpanded() {
    toggle.setAttribute("aria-expanded", String(panel.classList.contains(className) === expandedWhenPresent));
  }
  syncExpanded();
  toggle.onclick = function (event) {
    event.stopPropagation();
    panel.classList.toggle(className);
    syncExpanded();
  };
}

export function connectFieldLabels(root: HTMLElement) {
  root.querySelectorAll<HTMLLabelElement>(".field > label").forEach(function (label) {
    var control = label.parentElement.querySelector<HTMLInputElement | HTMLSelectElement | HTMLTextAreaElement>("input,select,textarea");
    if (!control) return;
    if (!control.id) control.id = "setting-" + (++controlId);
    label.htmlFor = control.id;
  });
}

export function makeCollapsibleCard(title, bodyElement, defaultCollapsed?, badgeEl?) {
  var card = el("div", "card");
  var header = el("div", "card-header");
  var h3 = document.createElement("h3");
  var toggle = el("button", "card-toggle");
  toggle.type = "button";
  toggle.textContent = title;
  h3.appendChild(toggle);
  var rightWrap = el("div", "card-header-right");
  if (badgeEl) rightWrap.appendChild(badgeEl);
  var chevron = el("span", "card-chevron");
  chevron.innerHTML = "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M6 9l6 6 6-6\"/></svg>";
  rightWrap.appendChild(chevron);
  header.appendChild(h3);
  header.appendChild(rightWrap);
  var body = el("div", "card-body");
  body.appendChild(bodyElement);
  card.appendChild(header);
  card.appendChild(body);
  if (defaultCollapsed) card.classList.add("collapsed");
  bindDisclosure(toggle, card, body, "collapsed", false);
  header.onclick = function () { toggle.click(); };
  return card;
}

// Shared controls: FIELD
export function field(labelText) {
  var f = el("div", "field");
  if (labelText) {
    var l = document.createElement("label");
    l.textContent = labelText;
    f.appendChild(l);
  }
  return f;
}
