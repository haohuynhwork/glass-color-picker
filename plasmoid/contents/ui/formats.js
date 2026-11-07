.pragma library

// Keep ids and output in sync with PickerOverlay::formatColor in the C++ picker.
var list = [
    { value: "hex",              text: "#RRGGBB" },
    { value: "hex-nohash",       text: "RRGGBB" },
    { value: "hex-lower",        text: "#rrggbb" },
    { value: "hex-lower-nohash", text: "rrggbb" },
    { value: "rgb-plain",        text: "RRR, GGG, BBB" },
    { value: "rgb",              text: "rgb" },
    { value: "rgba",             text: "rgba" },
    { value: "qt-rgba",          text: "Qt.rgba" },
    { value: "hsl",              text: "hsl" },
    { value: "hsv",              text: "hsv" },
    { value: "cmyk",             text: "cmyk" },
    { value: "latex",            text: "LaTeX" }
]

function format(hex, fmt) {
    hex = String(hex).toUpperCase()
    var r = parseInt(hex.substr(1, 2), 16), g = parseInt(hex.substr(3, 2), 16), b = parseInt(hex.substr(5, 2), 16)
    var rf = r / 255, gf = g / 255, bf = b / 255
    var max = Math.max(rf, gf, bf), min = Math.min(rf, gf, bf), d = max - min
    var f2 = function (v) { return v.toFixed(2) }
    var pct = function (v) { return Math.round(v * 100) }
    var hue = function () {
        if (d === 0) return 0
        var h = max === rf ? ((gf - bf) / d) % 6 : max === gf ? (bf - rf) / d + 2 : (rf - gf) / d + 4
        h = Math.round(h * 60)
        return h < 0 ? h + 360 : h
    }

    switch (fmt) {
    case "hex-nohash": return hex.substr(1)
    case "hex-lower": return hex.toLowerCase()
    case "hex-lower-nohash": return hex.substr(1).toLowerCase()
    case "rgb-plain": return r + ", " + g + ", " + b
    case "rgb": return "rgb(" + r + ", " + g + ", " + b + ")"
    case "rgba": return "rgba(" + r + ", " + g + ", " + b + ", 1)"
    case "qt-rgba": return "Qt.rgba(" + f2(rf) + ", " + f2(gf) + ", " + f2(bf) + ", 1)"
    case "hsl": {
        var l = (max + min) / 2
        var s = d === 0 ? 0 : d / (1 - Math.abs(2 * l - 1))
        return "hsl(" + hue() + ", " + pct(s) + "%, " + pct(l) + "%)"
    }
    case "hsv": return "hsv(" + hue() + ", " + pct(max === 0 ? 0 : d / max) + "%, " + pct(max) + "%)"
    case "cmyk": {
        var k = 1 - max
        var cmy = function (v) { return k >= 1 ? 0 : pct((1 - v - k) / (1 - k)) }
        return "cmyk(" + cmy(rf) + "%, " + cmy(gf) + "%, " + cmy(bf) + "%, " + pct(k) + "%)"
    }
    case "latex": return "\\definecolor{ColorName}{rgb}{" + f2(rf) + "," + f2(gf) + "," + f2(bf) + "}"
    default: return hex
    }
}
