(function (win) {
  'use strict';

  if (win._muiLoadedJS) return;
  win._muiLoadedJS = true;

  var doc = win.document;
  var overlayId = 'mui-overlay';
  var activeElement = null;
  var scrollLockPos = null;
  var keyupAttached = false;

  function addEvent(target, type, handler, options) {
    if (target) target.addEventListener(type, handler, options);
  }

  function removeEvent(target, type, handler, options) {
    if (target) target.removeEventListener(type, handler, options);
  }

  function getOverlayElement() {
    return doc.getElementById(overlayId);
  }

  function lockScroll() {
    if (scrollLockPos) return;

    scrollLockPos = {
      left: win.scrollX || win.pageXOffset || 0,
      top: win.scrollY || win.pageYOffset || 0
    };

    doc.body.style.position = 'fixed';
    doc.body.style.left = -scrollLockPos.left + 'px';
    doc.body.style.top = -scrollLockPos.top + 'px';
    doc.body.style.width = '100%';
  }

  function unlockScroll() {
    if (!scrollLockPos) return;

    doc.body.style.position = '';
    doc.body.style.left = '';
    doc.body.style.top = '';
    doc.body.style.width = '';

    win.scrollTo(scrollLockPos.left, scrollLockPos.top);
    scrollLockPos = null;
  }

  function onKeyup(ev) {
    if (ev.key === 'Escape' || ev.keyCode === 27) overlayOff();
  }

  function addKeyupHandler() {
    if (keyupAttached) return;
    keyupAttached = true;
    addEvent(doc, 'keyup', onKeyup);
  }

  function removeKeyupHandler() {
    if (!keyupAttached) return;
    keyupAttached = false;
    removeEvent(doc, 'keyup', onKeyup);
  }

  function onClick(ev) {
    if (ev.target && ev.target.id === overlayId) overlayOff();
  }

  function overlayOn(options, childElement) {
    var overlayEl = getOverlayElement();

    if (doc.activeElement) activeElement = doc.activeElement;

    lockScroll();

    if (!overlayEl) {
      overlayEl = doc.createElement('div');
      overlayEl.setAttribute('id', overlayId);
      overlayEl.setAttribute('tabindex', '-1');
      doc.body.appendChild(overlayEl);
    } else {
      while (overlayEl.firstChild) overlayEl.removeChild(overlayEl.firstChild);
    }

    if (childElement instanceof Element) {
      overlayEl.appendChild(childElement);
    }

    if (options.keyboard) addKeyupHandler();
    else removeKeyupHandler();

    removeEvent(overlayEl, 'click', onClick);
    if (!options.static) addEvent(overlayEl, 'click', onClick);

    overlayEl.muiOptions = options;
    overlayEl.focus();

    return overlayEl;
  }

  function overlayOff() {
    var overlayEl = getOverlayElement();
    var callbackFn;

    if (overlayEl) {
      while (overlayEl.firstChild) overlayEl.removeChild(overlayEl.firstChild);
      removeEvent(overlayEl, 'click', onClick);
      callbackFn = overlayEl.muiOptions && overlayEl.muiOptions.onclose;
      overlayEl.parentNode.removeChild(overlayEl);
    }

    removeKeyupHandler();
    unlockScroll();

    if (activeElement && typeof activeElement.focus === 'function') {
      activeElement.focus();
    }

    if (callbackFn) callbackFn();

    return overlayEl;
  }

  function overlay(action) {
    var overlayEl;
    var options;
    var childElement;
    var arg;
    var i;

    if (action === 'on') {
      for (i = arguments.length - 1; i > 0; i--) {
        arg = arguments[i];
        if (arg instanceof Element) childElement = arg;
        else if (arg && typeof arg === 'object') options = arg;
      }

      options = options || {};
      if (options.keyboard === undefined) options.keyboard = true;
      if (options.static === undefined) options.static = false;

      overlayEl = overlayOn(options, childElement);
    } else if (action === 'off') {
      overlayEl = overlayOff();
    } else {
      throw new Error("Expecting 'on' or 'off'");
    }

    return overlayEl;
  }

  win.mui = win.mui || {};
  win.mui.overlay = overlay;
})(window);
