// F12 exports the current handset view at 4x resolution, with and without
// the authored workbench background. The device-only PNG keeps transparency.
(() => {
    const outputScale = 4;
    // The UI artwork is shown at 2x its authored grid. Compose everything on
    // that original grid, then use one integer enlargement for every layer.
    const gridScale = outputScale * 2;
    let exporting = false;

    function makeCanvas(width, height) {
        const canvas = document.createElement('canvas');
        canvas.width = width;
        canvas.height = height;
        return canvas;
    }

    function notify(message, failed = false) {
        let toast = document.getElementById('screenshot-export-status');
        if (!toast) {
            toast = document.createElement('div');
            toast.id = 'screenshot-export-status';
            Object.assign(toast.style, {
                position: 'fixed', left: '50%', bottom: '18px', transform: 'translateX(-50%)',
                zIndex: '9999', padding: '9px 14px', color: '#28301d', background: '#c5c99c',
                border: '2px solid #353b27', boxShadow: '3px 3px 0 #313526',
                font: 'bold 13px/1.2 monospace', pointerEvents: 'none'
            });
            document.body.appendChild(toast);
        }
        toast.style.color = failed ? '#6d241c' : '#28301d';
        toast.textContent = message;
        toast.hidden = false;
        clearTimeout(toast.hideTimer);
        toast.hideTimer = setTimeout(() => { toast.hidden = true; }, 2800);
    }

    function drawHandset() {
        const width = 150, height = 290;
        const image = makeCanvas(width, height);
        const context = image.getContext('2d', { willReadFrequently: true });
        context.imageSmoothingEnabled = false;

        const phoneArt = document.getElementById('phone-art');
        const phoneSource = document.getElementById('phone-art-source');
        const artboard = document.getElementById('phone-artboard');

        // Recreate the authored silhouette shadow beneath the handset.
        const shadow = makeCanvas(width, height);
        const shadowContext = shadow.getContext('2d');
        shadowContext.imageSmoothingEnabled = false;
        shadowContext.drawImage(phoneSource, 0, 0, width, height);
        shadowContext.globalCompositeOperation = 'source-in';
        shadowContext.fillStyle = '#000';
        shadowContext.fillRect(0, 0, width, height);
        context.globalAlpha = 0.23;
        context.drawImage(shadow, 5, 6);
        context.globalAlpha = 1;
        context.drawImage(phoneArt, 0, 0, width, height);

        // Add only the screen's outer spill, leaving the center transparent for LCD pixels.
        const spill = makeCanvas(width, height);
        const spillContext = spill.getContext('2d');
        const spillStyle=getComputedStyle(document.getElementById('screen-spill'));
        spillContext.shadowColor=spillStyle.color;
        spillContext.shadowBlur=2;
        spillContext.fillStyle=spillStyle.color;
        spillContext.globalAlpha=0.17*Number(spillStyle.opacity);
        spillContext.fillRect(27,93,96,73);
        spillContext.shadowBlur=0;
        spillContext.globalAlpha=1;
        spillContext.globalCompositeOperation='destination-out';
        spillContext.fillRect(27,93,96,73);
        context.drawImage(spill, 0, 0);

        // One output cell per logical LCD pixel. The live 2x optical texture
        // must not be fractionally squeezed into the exported screen opening.
        const lcd = document.getElementById('lcd-texture');
        const optical=lcd.getContext('2d').getImageData(0,0,192,146).data;
        const logical=makeCanvas(96,73),logicalContext=logical.getContext('2d');
        const cells=logicalContext.createImageData(96,73);
        for(let y=0;y<73;y++)for(let x=0;x<96;x++){
            const out=(y*96+x)*4;
            for(let c=0;c<3;c++){
                let sum=0;
                for(let dy=0;dy<2;dy++)for(let dx=0;dx<2;dx++)sum+=optical[((y*2+dy)*192+x*2+dx)*4+c];
                cells.data[out+c]=Math.round(sum/4);
            }
            cells.data[out+3]=255;
        }
        logicalContext.putImageData(cells,0,0);
        context.fillStyle='#3e372a';context.fillRect(26,92,98,75);
        context.drawImage(logical,27,93);

        // Capture the individually layered, currently positioned button caps.
        const artRect = artboard.getBoundingClientRect();
        const keyScaleX = width / artRect.width;
        const keyScaleY = height / artRect.height;
        for (const socket of document.querySelectorAll('#key-art .key-socket')) {
            const key=socket.querySelector('.key-face'),recess=socket.querySelector('.key-recess');
            const rect=socket.getBoundingClientRect();
            const layer=makeCanvas(recess.width,recess.height),layerContext=layer.getContext('2d');
            layerContext.imageSmoothingEnabled=false;
            layerContext.drawImage(recess,0,0);
            // Inverse-project the tilted cap onto the authored pixel grid.
            // Bounding-box resizing loses the perspective; subpixel rasterizing
            // would break the equal-sized pixels of the exported artwork.
            const style=getComputedStyle(key);
            if(style.transform==='none')layerContext.drawImage(key,0,0);
            else {
                const m=new DOMMatrix(style.transform),origin=style.transformOrigin.split(' ').map(parseFloat);
                const sx=key.offsetWidth/key.width,sy=key.offsetHeight/key.height;
                const source=key.getContext('2d').getImageData(0,0,key.width,key.height);
                const projected=layerContext.createImageData(key.width,key.height);
                for(let py=0;py<key.height;py++)for(let px=0;px<key.width;px++){
                    const x=(px+.5)*sx-origin[0],y=(py+.5)*sy-origin[1];
                    const a=m.m11-x*m.m14,b=m.m21-x*m.m24,c=x*m.m44-m.m41;
                    const d=m.m12-y*m.m14,e=m.m22-y*m.m24,f=y*m.m44-m.m42;
                    const det=a*e-b*d;if(Math.abs(det)<1e-8)continue;
                    const u=Math.floor(((c*e-b*f)/det+origin[0])/sx);
                    const v=Math.floor(((a*f-c*d)/det+origin[1])/sy);
                    if(u>=0&&u<key.width&&v>=0&&v<key.height)
                        projected.data.set(source.data.subarray((v*key.width+u)*4,(v*key.width+u)*4+4),(py*key.width+px)*4);
                }
                const cap=makeCanvas(key.width,key.height);
                cap.getContext('2d').putImageData(projected,0,0);
                layerContext.drawImage(cap,0,0);
            }
            layerContext.globalCompositeOperation='destination-in';
            layerContext.drawImage(recess,0,0);
            const x=Math.round((rect.left-artRect.left)*keyScaleX),y=Math.round((rect.top-artRect.top)*keyScaleY);
            context.drawImage(layer,x,y,Math.round(rect.width*keyScaleX),Math.round(rect.height*keyScaleY));
        }
        return image;
    }

    function cropTransparent(canvas, padding = 4) {
        const context = canvas.getContext('2d', { willReadFrequently: true });
        const { data } = context.getImageData(0, 0, canvas.width, canvas.height);
        let left = canvas.width, top = canvas.height, right = -1, bottom = -1;
        for (let y = 0; y < canvas.height; y++) {
            for (let x = 0; x < canvas.width; x++) {
                if (data[(y * canvas.width + x) * 4 + 3] === 0) continue;
                left = Math.min(left, x); top = Math.min(top, y);
                right = Math.max(right, x); bottom = Math.max(bottom, y);
            }
        }
        if (right < left || bottom < top) return canvas;
        left = Math.max(0, left - padding); top = Math.max(0, top - padding);
        right = Math.min(canvas.width - 1, right + padding);
        bottom = Math.min(canvas.height - 1, bottom + padding);
        const cropped = makeCanvas(right - left + 1, bottom - top + 1);
        cropped.getContext('2d').drawImage(canvas, left, top, cropped.width, cropped.height, 0, 0, cropped.width, cropped.height);
        return cropped;
    }

    function upscale(source, factor) {
        const result = makeCanvas(source.width * factor, source.height * factor);
        const context = result.getContext('2d');
        context.imageSmoothingEnabled = false;
        context.drawImage(source, 0, 0, result.width, result.height);
        return result;
    }

    function download(canvas, filename) {
        return new Promise((resolve, reject) => {
            canvas.toBlob(blob => {
                if (!blob) { reject(new Error('PNG export failed')); return; }
                const url = URL.createObjectURL(blob);
                const link = document.createElement('a');
                link.href = url;
                link.download = filename;
                link.style.display = 'none';
                document.body.appendChild(link);
                link.click();
                link.remove();
                setTimeout(() => URL.revokeObjectURL(url), 10000);
                resolve();
            }, 'image/png');
        });
    }

    async function makeWorkbench(handset) {
        const container = document.getElementById('device-container');
        const artboard = document.getElementById('phone-artboard');
        const containerRect = container.getBoundingClientRect();
        const artboardRect = artboard.getBoundingClientRect();
        const hostScale = containerRect.width / 320;
        const x = (artboardRect.left - containerRect.left) / hostScale;
        const y = (artboardRect.top - containerRect.top) / hostScale;

        const background = new Image();
        background.src = new URL('workbench-pixel.svg', document.baseURI).href;
        await background.decode();

        const scene = makeCanvas(160, 220);
        const context = scene.getContext('2d');
        context.imageSmoothingEnabled = false;
        context.drawImage(background, 0, 0, 160, 220);
        context.drawImage(handset, Math.round(x/2), Math.round(y/2), 150, 290);
        return scene;
    }

    window.exportPhoneScreenshots = async () => {
        if (exporting) return;
        exporting = true;
        notify('Preparing 4x screenshots…');
        try {
            const source = document.getElementById('phone-art-source');
            if (!source.complete || !source.naturalWidth) await source.decode();
            const handset = drawHandset();
            const isolated = upscale(cropTransparent(handset), gridScale);
            const withBackground = upscale(await makeWorkbench(handset), gridScale);
            await Promise.all([
                download(isolated, 'Scrap-Brain-2G-device-4x.png'),
                download(withBackground, 'Scrap-Brain-2G-device-with-background-4x.png')
            ]);
            notify('Saved both 4x screenshots');
        } catch (error) {
            console.error('Screenshot export failed:', error);
            notify('Screenshot export failed', true);
        } finally {
            exporting = false;
        }
    };
})();
