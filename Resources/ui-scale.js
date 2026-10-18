// Quiet workbench controls, separate from the handset's LCD.
(() => {
    const scene=document.getElementById('device-container'),dpr=()=>window.devicePixelRatio||1;
    // A window size is only offered when one artwork pixel (2 CSS px) covers a whole number of
    // device pixels, so every art and LCD pixel is the same size at any Windows display scale.
    const sizes=(()=>{
        const limit=Math.max(440,(screen.availHeight||0)-40),list=[];
        for(let k=1;k<=12;k++){
            const percent=Math.round(k/(2*dpr())*10000)/100;
            if(percent>=40&&(!list.length||440*percent/100<=limit))list.push(percent);
        }
        return list.sort((a,b)=>a-b);
    })();
    const nearestSize=value=>sizes.reduce((best,size)=>{
        const a=Math.abs(size-value),b=Math.abs(best-value);return a<b||a===b&&size>best?size:best;
    },sizes[0]);
    const menu=document.createElement('div');menu.className='desk-menu';menu.hidden=true;
    menu.setAttribute('role','menu');menu.setAttribute('aria-label','Instrument options');
    menu.innerHTML=`<div class="desk-size-row"><button class="desk-size" role="menuitem" aria-haspopup="menu" aria-expanded="false">Display size<span class="desk-arrow">›</span></button><div class="desk-submenu" role="menu" aria-label="Display size" hidden></div></div>
        <button class="desk-import" role="menuitem">Import audio…</button>`;
    document.body.appendChild(menu);
    const options=menu.querySelector('.desk-submenu'),sizeButton=menu.querySelector('.desk-size');
    let selected=nearestSize(120),previousFocus=null,busy=false;
    try{const saved=Number(localStorage.getItem('2g-display-size'));if(Number.isFinite(saved)&&saved>0)selected=nearestSize(saved);}catch(_){}
    function fitScene(){
        // Snap to whole device pixels per art pixel; the window is sized to match, so any leftover is under one pixel.
        const step=1/(2*dpr()),fit=Math.min(view().w/320,view().h/440),whole=Math.round(fit/step);
        // 100% is always offered; on a fractional display scale it is not pixel-aligned, so only snap when the window already is.
        // Demo mode always rounds down to a whole step, so the handset stays pixel-perfect however big the window is.
        const demo=document.documentElement.classList.contains('demo-mode');
        const scale=demo?Math.max(1,Math.floor(fit/step+.02))*step:whole>=1&&Math.abs(fit/step-whole)<.03?whole*step:fit;
        scene.style.setProperty('--ui-scale',String(scale));
    }
    const view=()=>window.viewSize?.()||{w:innerWidth,h:innerHeight};
    // When the window is resized from outside (a host dragging its frame), remember the size that was snapped to.
    let followWindow=false;
    function followWindowSize(){
        if(!followWindow||document.documentElement.classList.contains('demo-mode')||busy)return;
        const percent=nearestSize(view().w/320*100);
        if(percent!==selected){selected=percent;try{localStorage.setItem('2g-display-size',String(percent));}catch(_){}markSelection();}
    }
    window.addEventListener('resize',()=>{fitScene();close();followWindowSize();});fitScene();
    if(typeof ResizeObserver==='function')new ResizeObserver(fitScene).observe(document.documentElement);
    function close(){
        menu.hidden=true;options.hidden=true;sizeButton.setAttribute('aria-expanded','false');
        if(menu.contains(document.activeElement)&&previousFocus?.isConnected)previousFocus.focus({preventScroll:true});
    }
    function markSelection(){options.querySelectorAll('button').forEach(button=>{
        const checked=Number(button.dataset.size)===selected;button.setAttribute('aria-checked',String(checked));
        button.querySelector('.desk-check').textContent=checked?'●':'';
    });}
    function openSizes(focus=false){
        options.hidden=false;sizeButton.setAttribute('aria-expanded','true');options.classList.remove('opens-left');options.style.top='0px';
        if(options.getBoundingClientRect().right>view().w-4)options.classList.add('opens-left');
        const rect=options.getBoundingClientRect();if(rect.bottom>view().h-4)options.style.top=`${Math.min(0,view().h-4-rect.bottom)}px`;
        if(focus)options.querySelector(`[data-size="${selected}"]`).focus({preventScroll:true});
    }
    async function selectSize(percent){
        if(busy||!sizes.includes(percent))return;
        const resize=window.__JUCE__?.backend?.getNativeFunction?.('setUiScale');busy=true;
        try{
            if(resize){let result=await resize(percent);if(Array.isArray(result))result=result[0];if(result!==true)throw Error('Window size rejected');}
            else if(window.__JUCE__?.backend)throw Error('Display size unavailable');
            else scene.style.setProperty('--ui-scale',String(percent/100));
            selected=percent;try{localStorage.setItem('2g-display-size',String(percent));}catch(_){}
            markSelection();close();
            // Let the page see the new window size before the next step, and re-fit after it settles: a fast drag
            // used to leave the handset small inside a window that had already grown.
            await new Promise(resolve=>{
                const timer=setTimeout(done,250);
                function done(){clearTimeout(timer);window.removeEventListener('resize',done);resolve();}
                window.addEventListener('resize',done,{once:true});
            });
            fitScene();setTimeout(fitScene,120);setTimeout(fitScene,400);
        }catch(error){console.warn(error.message);}finally{busy=false;}
    }
    sizes.forEach(percent=>{
        const button=document.createElement('button');button.type='button';button.dataset.size=String(percent);
        button.setAttribute('role','menuitemradio');button.innerHTML=`<span class="desk-check"></span>${Math.round(percent)}%`;
        button.addEventListener('click',()=>selectSize(percent));options.appendChild(button);
    });
    // Corner grip: drag to resize. It only ever lands on the sizes above, so the pixel art never goes soft.
    const grip=document.createElement('canvas');grip.className='resize-grip';grip.width=8;grip.height=8;
    grip.setAttribute('aria-hidden','true');grip.title='Drag to resize';document.body.appendChild(grip);
    const gripContext=grip.getContext('2d');
    for(const [x,y] of [[6,6],[4,6],[2,6],[6,4],[4,4],[6,2]]){
        gripContext.fillStyle='#c79a66';gripContext.fillRect(x+1,y+1,1,1);
        gripContext.fillStyle='#5b3926';gripContext.fillRect(x,y,1,1);
    }
    let dragging=false,grabX=0,grabY=0,wanted=null,pumping=false;
    async function pump(){
        if(pumping)return;pumping=true;
        try{
            for(let attempts=0;wanted!==null&&wanted!==selected&&attempts<60;attempts++){
                if(busy){await new Promise(resolve=>setTimeout(resolve,30));continue;}
                await selectSize(wanted);
                await new Promise(resolve=>setTimeout(resolve,120)); // never faster than the window can follow
            }
        }finally{pumping=false;}
    }
    grip.addEventListener('pointerdown',event=>{
        dragging=true;grabX=view().w-event.clientX;grabY=view().h-event.clientY;
        grip.setPointerCapture(event.pointerId);event.preventDefault();
    });
    grip.addEventListener('pointermove',event=>{
        if(!dragging)return;
        wanted=nearestSize(Math.max((event.clientX+grabX)/320,(event.clientY+grabY)/440)*100);pump();
    });
    const releaseGrip=()=>{dragging=false;pump().finally(()=>{wanted=null;fitScene();setTimeout(fitScene,150);setTimeout(fitScene,500);});};
    grip.addEventListener('pointerup',releaseGrip);grip.addEventListener('pointercancel',releaseGrip);
    try{window.__JUCE__?.backend?.getNativeFunction?.('setResizeSteps')?.(sizes)?.catch?.(()=>{});}catch(_){}
    sizeButton.addEventListener('click',()=>openSizes(true));
    menu.querySelector('.desk-size-row').addEventListener('pointerenter',()=>openSizes());
    for(const button of menu.querySelectorAll('.desk-import'))button.addEventListener('pointerenter',()=>{
        options.hidden=true;sizeButton.setAttribute('aria-expanded','false');
    });
    menu.querySelector('.desk-import').addEventListener('click',()=>{close();if(typeof beginSoundImport==='function')beginSoundImport();});
    function isEmptyDesk(event){
        if(event.target.closest('#screen-frame,.phone-key,.desk-menu,.master-volume'))return false;
        const art=document.getElementById('phone-art'),rect=art.getBoundingClientRect();
        const x=Math.floor((event.clientX-rect.left)*art.width/rect.width),y=Math.floor((event.clientY-rect.top)*art.height/rect.height);
        if(x<0||y<0||x>=art.width||y>=art.height)return true;
        try{return art.getContext('2d').getImageData(x,y,1,1).data[3]<16;}catch(_){return false;}
    }
    window.addEventListener('pointerdown',event=>{
        if(!menu.contains(event.target))close();
    },true);
    window.addEventListener('contextmenu',event=>{
        event.preventDefault();
        if(document.documentElement.classList.contains('demo-mode'))return;
        if(!isEmptyDesk(event))return;
        event.preventDefault();event.stopImmediatePropagation();previousFocus=document.activeElement;markSelection();menu.hidden=false;
        menu.style.left=`${Math.max(4,Math.min(event.clientX,view().w-menu.offsetWidth-4))}px`;
        menu.style.top=`${Math.max(4,Math.min(event.clientY,view().h-menu.offsetHeight-4))}px`;
        sizeButton.focus({preventScroll:true});
    },true);
    window.addEventListener('blur',close);
    window.addEventListener('keydown',event=>{
        if(menu.hidden)return;event.stopImmediatePropagation();const inSub=options.contains(document.activeElement);
        if(event.key==='Escape'||event.key==='ArrowLeft'&&inSub){event.preventDefault();
            if(inSub){options.hidden=true;sizeButton.setAttribute('aria-expanded','false');sizeButton.focus();}else close();return;}
        if(event.key==='ArrowRight'&&document.activeElement===sizeButton){event.preventDefault();openSizes(true);return;}
        if(['ArrowUp','ArrowDown','Tab','Home','End'].includes(event.key)){
            event.preventDefault();const buttons=inSub?[...options.querySelectorAll('button')]:[sizeButton,menu.querySelector('.desk-import')];
            const index=buttons.indexOf(document.activeElement),delta=event.key==='ArrowUp'||event.key==='Tab'&&event.shiftKey?-1:1;
            buttons[event.key==='Home'?0:event.key==='End'?buttons.length-1:(index+delta+buttons.length)%buttons.length].focus();
        }else if(!['Enter',' '].includes(event.key))event.preventDefault();
    },true);
    const volume=document.createElement('label');volume.className='master-volume';
    volume.innerHTML='<canvas class="volume-strip" width="40" height="8" aria-hidden="true"></canvas><input id="master-volume" type="range" min="0" max="200" step="0.25" value="100" aria-label="Main volume">';
    scene.appendChild(volume);
    const slider=volume.querySelector('input');
    const plugin=Boolean(window.__JUCE__?.initialisationData?.isPluginHost);
    const volumeCanvas=volume.querySelector('.volume-strip'),volumeContext=volumeCanvas.getContext('2d');
    let volumeTouched=false,volumeFrame=0,fineVolume=false;
    // Engraved speaker: columns 5 and 6-7 are the two sound waves, shown by level.
    const speaker=['...#....','..##..#.','####.#.#','####.#.#','####.#.#','..##..#.','...#....'];
    // The slider is the plain kind plug-ins use: a thin track, a short knob. Colours stay inside the wood palette.
    const TRACK_X0=13,TRACK_X1=37; // knob centre runs between these columns; the range input covers exactly this span
    function paintVolume(){
        const level=Number(slider.value),waves=level===0?0:level<60?1:2;
        volumeContext.clearRect(0,0,40,8);
        for(const [color,shift] of [['#bc8854',1],['#68442f',0]]){
            volumeContext.fillStyle=color;
            speaker.forEach((row,y)=>{for(let x=0;x<8;x++)if(row[x]==='#'&&(x<5||(x===5?waves>=1:waves>=2)))volumeContext.fillRect(x,y+shift,1,1);});
        }
        const knobX=Math.round(TRACK_X0+level/200*(TRACK_X1-TRACK_X0));
        // Two-pixel track: darker to the right of the knob, a little lighter to its left.
        volumeContext.fillStyle='#68442f';volumeContext.fillRect(TRACK_X0-1,3,TRACK_X1-TRACK_X0+3,1);
        volumeContext.fillStyle='#795035';volumeContext.fillRect(TRACK_X0-1,4,TRACK_X1-TRACK_X0+3,1);
        volumeContext.fillStyle='#bc8854';volumeContext.fillRect(TRACK_X0-1,3,knobX-TRACK_X0+1,1);
        volumeContext.fillStyle='#aa794c';volumeContext.fillRect(TRACK_X0-1,4,knobX-TRACK_X0+1,1);
        // Knob: a short block, one step lighter than the track, with a darker foot.
        volumeContext.fillStyle='#d2a672';volumeContext.fillRect(knobX-1,1,3,6); // centred on the two track rows
        slider.setAttribute('aria-valuetext',level===0?'Muted':`${slider.value}%`);
    }
    const gain=()=>Number(slider.value)<=100?Math.pow(Number(slider.value)/100,2):Number(slider.value)/100;
    window.getMasterVolumeGain=gain;
    function setVolume(event){
        if(event?.type==='input'&&!fineVolume){
            const value=Number(slider.value),nearest=Math.round(value/5)*5;
            if(Math.abs(value-nearest)<.8)slider.value=String(nearest);
        }
        paintVolume();volumeTouched=true;
        if(!plugin)try{localStorage.setItem('2g-master-volume',slider.value);}catch(_){}
        cancelAnimationFrame(volumeFrame);
        volumeFrame=requestAnimationFrame(()=>{
            window.__JUCE__?.backend?.getNativeFunction?.('setMasterVolume')?.(gain()).catch(error=>console.warn(error.message));
        });
    }
    slider.addEventListener('input',setVolume);
    slider.addEventListener('pointerdown',event=>{fineVolume=event.shiftKey;});
    slider.addEventListener('pointermove',event=>{if(event.buttons)fineVolume=event.shiftKey;});
    slider.addEventListener('pointerup',()=>{fineVolume=false;});
    slider.addEventListener('dblclick',()=>{slider.value='100';setVolume();});
    slider.addEventListener('wheel',event=>{
        if(event.ctrlKey)return;event.preventDefault();event.stopImmediatePropagation();
        slider.value=String(Math.max(0,Math.min(200,Number(slider.value)+(event.deltaY<0?2:-2))));setVolume();
    },{passive:false});
    window.addEventListener('keydown',event=>{
        if(event.target!==slider)return;event.stopImmediatePropagation();
        const direction={ArrowUp:1,ArrowRight:1,ArrowDown:-1,ArrowLeft:-1}[event.key];
        if(direction||event.key==='Home'||event.key==='End'){
            event.preventDefault();slider.value=String(event.key==='Home'?0:event.key==='End'?200:Math.max(0,Math.min(200,Number(slider.value)+direction*(event.shiftKey?.25:1))));setVolume();
        }
    },true);
    function applyVolume(raw){
        while(Array.isArray(raw)&&raw.length===1)raw=raw[0];
        const value=Number(raw);if(!Number.isFinite(value))return;
        const bounded=Math.max(0,Math.min(2,value));
        slider.value=String(Math.round((bounded<=1?Math.sqrt(bounded):bounded)*100));paintVolume();
    }
    window.__JUCE__?.backend?.addEventListener?.('masterVolumeChanged',applyVolume);
    let saved=null;if(!plugin)try{saved=localStorage.getItem('2g-master-volume');}catch(_){}
    if(saved!==null&&Number.isFinite(Number(saved))){slider.value=String(Math.max(0,Math.min(200,Number(saved))));setVolume();}
    else window.__JUCE__?.backend?.getNativeFunction?.('getMasterVolume')?.().then(value=>{if(!volumeTouched)applyVolume(value);}).catch(error=>console.warn(error.message));
    paintVolume();
    if(window.__JUCE__?.backend?.getNativeFunction?.('setUiScale'))selectSize(selected).finally(()=>setTimeout(()=>{followWindow=true;},800));
    else followWindow=true;
})();
