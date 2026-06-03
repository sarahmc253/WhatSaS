(function () {
    const canvas = document.createElement('canvas');
    canvas.width = canvas.height = 64;
    const ctx = canvas.getContext('2d');
    ctx.font = '52px serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText('🧸', 32, 36);
    const link = document.getElementById('favicon');
    if (link) link.href = canvas.toDataURL();
}());
