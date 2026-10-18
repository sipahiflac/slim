// JUCE 8 native-function protocol (see JUCE native/javascript/index.js).
// Event listeners are provided by JUCE; Promise bindings are frontend helpers.
(() => {
    const backend=window.__JUCE__?.backend;
    if(!backend?.addEventListener || backend.getNativeFunction)return;
    const pending=new Map();let nextId=0;
    backend.addEventListener('__juce__complete',({promiseId,result})=>{
        const p=pending.get(promiseId);if(!p)return;
        clearTimeout(p.timer);pending.delete(promiseId);p.resolve(result);
    });
    backend.getNativeFunction=name=>{
        const names=window.__JUCE__.initialisationData?.__juce__functions || [];
        if(!names.includes(name))return null;
        return (...params)=>new Promise((resolve,reject)=>{
            const resultId=nextId++;
            const timer=setTimeout(()=>{pending.delete(resultId);reject(new Error('Native call timed out: '+name));},90000);
            pending.set(resultId,{resolve,reject,timer});
            backend.emitEvent('__juce__invoke',{name,params,resultId});
        });
    };
})();
