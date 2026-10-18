// Independent of the handset renderer: visible before artwork or app setup.
(() => {
    const layer=document.getElementById('boot-screen'),canvas=document.getElementById('boot-canvas');
    const context=canvas.getContext('2d'),started=performance.now();
    const steps=['DISPLAY','ARTWORK','LIBRARY','AUDIO'],done=new Set(),entries=[];
    // Every line carries the moment it was actually produced, like a kernel log.
    const log=line=>entries.push({at:performance.now()-started,text:String(line)});
    log('2G SYSTEM / BOOT');log('JS RUNTIME READY');
    // The page is larger than the window: the native side keeps the web view at desktop size and reports the part
    // the window shows (the "view"). Everything that used innerWidth/innerHeight uses window.viewSize() instead.
    const unwrap=value=>{while(Array.isArray(value))value=value[0];return value;};
    let viewW=Number(unwrap(window.__JUCE__?.initialisationData?.viewWidth))||innerWidth;
    let viewH=Number(unwrap(window.__JUCE__?.initialisationData?.viewHeight))||innerHeight;
    const rootStyle=document.documentElement.style;
    rootStyle.setProperty('--view-w',viewW+'px');rootStyle.setProperty('--view-h',viewH+'px');
    window.viewSize=()=>({w:viewW,h:viewH});
    function setView(raw){
        const size=unwrap(raw),w=Math.round(Number(size?.w)),h=Math.round(Number(size?.h));
        if(!(w>0&&h>0)||w===viewW&&h===viewH)return;
        viewW=w;viewH=h;rootStyle.setProperty('--view-w',w+'px');rootStyle.setProperty('--view-h',h+'px');
        window.dispatchEvent(new Event('resize'));
    }
    const viewBackend=window.__JUCE__?.backend;
    if(viewBackend){
        viewBackend.addEventListener?.('viewSize',setView);
        viewBackend.getNativeFunction?.('getViewSize')?.().then(setView).catch(()=>{});
    }else window.addEventListener('resize',()=>setView({w:innerWidth,h:innerHeight}));
    let showConsole=window.__JUCE__?.initialisationData?.showStartupConsole;
    while(Array.isArray(showConsole))showConsole=showConsole[0];
    let replay=false;
    try{replay=!!sessionStorage.getItem('2g.demo-replay');}catch(_){}
    const silent=showConsole===false&&!replay; // a demo-mode replay always shows the console
    let failed=false,finished=false,frame=0;
    const glyphs={
        A:[14,17,17,31,17,17,17],B:[30,17,17,30,17,17,30],C:[14,17,16,16,16,17,14],
        D:[30,17,17,17,17,17,30],E:[31,16,16,30,16,16,31],F:[31,16,16,30,16,16,16],
        G:[14,17,16,23,17,17,15],H:[17,17,17,31,17,17,17],I:[14,4,4,4,4,4,14],
        J:[7,2,2,2,18,18,12],K:[17,18,20,24,20,18,17],L:[16,16,16,16,16,16,31],
        M:[17,27,21,21,17,17,17],N:[17,25,21,19,17,17,17],O:[14,17,17,17,17,17,14],
        P:[30,17,17,30,16,16,16],Q:[14,17,17,17,21,18,13],R:[30,17,17,30,20,18,17],
        S:[15,16,16,14,1,1,30],T:[31,4,4,4,4,4,4],U:[17,17,17,17,17,17,14],
        V:[17,17,17,17,17,10,4],W:[17,17,17,21,21,21,10],X:[17,17,10,4,10,17,17],
        Y:[17,17,10,4,4,4,4],Z:[31,1,2,4,8,16,31],2:[14,17,1,2,4,8,31],
        '.':[0,0,0,0,0,0,4],'-':[0,0,0,31,0,0,0]
    };
    function text(x,y,value){
        for(const ch of value){const rows=glyphs[ch];
            if(rows)rows.forEach((bits,row)=>{for(let col=0;col<5;col++)if(bits&(16>>col))context.fillRect(x+col,y+row,1,1);});
            x+=ch===' '?4:6;
        }
    }
    Object.assign(glyphs,{
        0:[14,17,19,21,25,17,14],1:[4,12,4,4,4,4,14],3:[30,1,1,14,1,1,30],
        4:[2,6,10,18,31,2,2],5:[31,16,16,30,1,1,30],6:[14,16,16,30,17,17,14],
        7:[31,1,2,4,8,8,8],8:[14,17,17,14,17,17,14],9:[14,17,17,15,1,1,14],
        '/':[1,2,2,4,8,8,16],':':[0,4,4,0,4,4,0],'>':[16,8,4,2,4,8,16],
        '[':[14,8,8,8,8,8,14],']':[14,2,2,2,2,2,14]
    });
    function paint(){
        const scale=viewW>=280?2:1;
        const width=Math.max(1,Math.floor(viewW/scale)),height=Math.max(1,Math.floor(viewH/scale));
        if(canvas.width!==width||canvas.height!==height){canvas.width=width;canvas.height=height;}
        canvas.style.width=`${width*scale}px`;canvas.style.height=`${height*scale}px`;
        context.fillStyle='#1e1e1e';context.fillRect(0,0,width,height);
        if(silent&&!failed)return;
        context.fillStyle='#b6c777';
        const columns=Math.max(8,Math.floor((width-12)/6)),stampColumns=8,body=Math.max(1,columns-stampColumns),lines=[];
        const active=steps.find(step=>!done.has(step));
        const caughtUp=shown>=entries.length;
        for(const entry of entries.slice(0,shown)){
            const stamp=`[${(entry.at/1000).toFixed(3)}]`;
            for(let start=0;start<Math.max(1,entry.text.length);start+=body)lines.push({stamp:start?'':stamp,text:entry.text.slice(start,start+body)});
        }
        if(failed)lines.push({stamp:'',text:'ERROR / CLICK TO RETRY'});
        else if(caughtUp&&!finished)lines.push({stamp:'',text:`> ${active||'READY'}`,cursor:true});
        const visible=Math.max(1,Math.floor((height-12)/9)),textX=6+stampColumns*6;
        lines.slice(-visible).forEach((line,index)=>{
            const y=6+index*9;
            context.fillStyle='#6f8a3a';text(6,y,line.stamp);
            context.fillStyle='#b6c777';text(textX,y,line.text);
            if(line.cursor&&Math.floor(ticks/18)%2===0){
                let cursorX=textX+1;for(const ch of line.text)cursorX+=ch===' '?4:6;
                context.fillRect(cursorX,y,5,7);
            }
        });
    }
    // PS1-style exit: the console is wiped in one hard cut to black with only the small green logo (drawn at
    // its true pixel size), held for a beat, then faded out smoothly to reveal the phone. Hard in, soft out.
    const logo=new Image();logo.src='boot-logo.png';
    const logoReady=new Promise(resolve=>{logo.complete?resolve():(logo.onload=logo.onerror=resolve);});
    let dissolving=false;
    async function dissolve(){
        dissolving=true;
        await logoReady;
        const w=canvas.width,h=canvas.height;
        context.fillStyle='#000';context.fillRect(0,0,w,h);
        if(logo.naturalWidth){
            const tint=document.createElement('canvas');tint.width=logo.naturalWidth;tint.height=logo.naturalHeight;
            const tc=tint.getContext('2d');tc.drawImage(logo,0,0);
            tc.globalCompositeOperation='source-in';tc.fillStyle='#b6c777';tc.fillRect(0,0,tint.width,tint.height);
            context.drawImage(tint,Math.round((w-tint.width)/2),Math.round((h-tint.height)/2));
        }
        layer.style.background='#000';layer.style.pointerEvents='none';
        await new Promise(resolve=>setTimeout(resolve,700));
        layer.style.transition='opacity 600ms ease-out';layer.style.opacity='0';
        await new Promise(resolve=>setTimeout(resolve,620));
    }
    window.addEventListener('resize',()=>{if(!dissolving)paint();});
    log(`VIEW ${viewW}X${viewH}`);
    log(`HOST ${window.__JUCE__?.initialisationData?.isPluginHost?'VST3':'STANDALONE'}`);
    // Lines print one at a time like a real boot log; a backlog prints faster so
    // the reveal never holds startup back.
    let shown=0,ticks=0,wait=0,onCaughtUp=null;
    const timer=setInterval(()=>{
        ticks++;frame=Math.floor(ticks/7);
        // Irregular cadence: mostly steady, sometimes a short hitch before the next line.
        if(wait>0)wait--;
        else if(shown<entries.length){
            shown=Math.min(entries.length,shown+(entries.length-shown>8?2:1));
            const roll=Math.random();wait=roll<.08?3+Math.floor(Math.random()*3):roll<.35?1:0;
        }
        paint();
        if(shown>=entries.length&&onCaughtUp){const resolve=onCaughtUp;onCaughtUp=null;resolve();}
    },28);paint();
    function imageReady(image){
        return new Promise((resolve,reject)=>{
            const finish=()=>image.naturalWidth?resolve():reject(new Error('Artwork unavailable'));
            if(image.complete)finish();
            else{image.addEventListener('load',finish,{once:true});image.addEventListener('error',()=>reject(new Error('Artwork unavailable')),{once:true});}
        }).then(()=>image.decode?.());
    }
    const background=new Image();background.src='workbench-pixel.svg';
    // Catch immediately; the caller reports failures once app setup is complete.
    const assets=Promise.all([...document.images,background].map(imageReady));
    assets.catch(()=>{});
    window.phoneBoot={
        assets,
        mark(step){if(!done.has(step))log(`${step} READY`);done.add(step);paint();layer.setAttribute('aria-label',`Starting 2G: ${step.toLowerCase()} ready`);},
        info(lines){lines.forEach(line=>log(String(line).toUpperCase()));paint();},
        fail(){failed=true;shown=entries.length;clearInterval(timer);paint();layer.setAttribute('aria-label','Could not finish loading. Click to retry.');},
        async finish(){
            if(finished)return;finished=true;
            log(`READY IN ${Math.round(performance.now()-started)} MS`);
            if(!silent){
                const printed=shown>=entries.length?Promise.resolve():new Promise(resolve=>{onCaughtUp=resolve;});
                await Promise.all([printed,window.__JUCE__?.backend?.getNativeFunction?.('startupConsoleShown')?.()]);
                await new Promise(resolve=>setTimeout(resolve,140));
            }
            await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
            clearInterval(timer);document.documentElement.classList.remove('booting');
            if(silent){layer.remove();window.dispatchEvent(new Event('phoneReady'));return;}
            shown=entries.length;paint();
            await dissolve();
            layer.remove();window.dispatchEvent(new Event('phoneReady'));
        }
    };
    layer.addEventListener('click',()=>{if(failed)location.reload();});
    window.addEventListener('keydown',event=>{
        if(!document.documentElement.classList.contains('booting'))return;
        event.preventDefault();event.stopImmediatePropagation();
        if(failed&&event.key==='Enter')location.reload();
    },true);
})();
