function loadResource(tagName, attributes, callback) {
    var element = document.createElement(tagName);
    for (var key in attributes) {
        element[key] = attributes[key];
    }
    element.onload = function() {
        console.log(attributes.href || attributes.src, 'has loaded');
        if (typeof callback === 'function') {
            callback();
        }
    };
    document.head.appendChild(element);
}

let inline_load_idx = 0;
function loadCSSAndJS() {
    if (inline_load_idx >= inline_loader_list.length) {
        if (typeof inline_loader_ondone === 'function') {
            inline_loader_ondone();
        }
        return;
    }
    let item = inline_loader_list[inline_load_idx];
    loadResource(item.tag, item.attribs, function () {
        inline_load_idx += 1;
        loadCSSAndJS();
    });
}

document.addEventListener('DOMContentLoaded', loadCSSAndJS, false);
