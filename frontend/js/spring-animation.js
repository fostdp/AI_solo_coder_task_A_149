const SpringAnimation = (function () {
    let scene, camera, renderer, springMesh, springGroup;
    let container, canvas;
    let animationId = null;
    let currentTorsion = 0;
    let targetTorsion = 0;
    let cycleCount = 0;
    let damageRatio = 0;
    let springConfig = {
        wireDiameter: 0.02,
        coilMeanDiameter: 0.15,
        activeCoils: 12,
        material: TrebuchetPhysics.MATERIALS.steel65mn
    };

    const springVertexShader = `
        uniform float uTorsion;
        uniform float uTurns;
        uniform float uSpringHeight;
        uniform float uSpringRadius;
        uniform float uWireRadius;
        uniform float uDamage;
        uniform float uTime;
        uniform float uCycleCount;
        uniform float uElapsed;

        attribute float aSegmentIndex;
        attribute float aRingIndex;

        varying vec3 vNormal;
        varying float vDepth;
        varying float vStress;
        varying vec3 vWorldPos;

        #define PI 3.14159265359

        void main() {
            float totalSegments = 512.0;
            float totalRings = 16.0;
            float t = aSegmentIndex / totalSegments;
            float ringAngle = aRingIndex / totalRings * 2.0 * PI;

            float compressionFactor = 1.0 + sin(uTorsion * 0.3) * 0.15;
            float springH = uSpringHeight * compressionFactor;
            float springR = uSpringRadius * (1.0 + sin(t * PI) * 0.08);

            float plasticBuckle = 0.0;
            if (uDamage > 0.5) {
                plasticBuckle = sin(t * PI * 3.0 + uTime * 0.5) * 0.02 * (uDamage - 0.5) * 2.0;
            }

            float baseAngle = t * uTurns * 2.0 * PI;
            float dynamicAngle = baseAngle + uTorsion * (t - 0.5) * 1.2;

            float centerX = cos(dynamicAngle) * springR;
            float centerY = -t * springH + springH * 0.5 + plasticBuckle;
            float centerZ = sin(dynamicAngle) * springR;

            vec3 tangent = normalize(vec3(
                -sin(dynamicAngle) * springR,
                -springH / (uTurns * 2.0 * PI),
                cos(dynamicAngle) * springR
            ));
            vec3 up = vec3(0.0, 1.0, 0.0);
            vec3 normalAxis = normalize(cross(tangent, up));
            vec3 binormal = normalize(cross(tangent, normalAxis));

            float damageThinning = 1.0 - uDamage * 0.25;
            float wireR = uWireRadius * damageThinning;
            float vibration = 0.0;
            float elapsed = uElapsed;
            if (elapsed < 0.8) {
                vibration = sin(elapsed * 60.0) * exp(-elapsed * 6.0) * 0.008;
            }
            wireR += vibration;

            vec3 offset = normalAxis * cos(ringAngle) * wireR
                        + binormal * sin(ringAngle) * wireR;

            vec3 finalPos = vec3(centerX, centerY, centerZ) + offset;

            vStress = abs(sin(dynamicAngle)) * (abs(uTorsion) / 3.5);
            vDepth = t;
            vWorldPos = finalPos;

            vec3 ringNormal = normalize(offset);
            vNormal = ringNormal;

            gl_Position = projectionMatrix * modelViewMatrix * vec4(finalPos, 1.0);
        }
    `;

    const springFragmentShader = `
        uniform float uTorsion;
        uniform float uDamage;
        uniform float uTime;

        varying vec3 vNormal;
        varying float vDepth;
        varying float vStress;
        varying vec3 vWorldPos;

        vec3 hsv2rgb(vec3 c) {
            vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
            vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
            return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
        }

        void main() {
            vec3 lightDir = normalize(vec3(0.5, 1.0, 0.8));
            vec3 viewDir = normalize(cameraPosition - vWorldPos);

            float diff = max(dot(vNormal, lightDir), 0.0);
            vec3 halfDir = normalize(lightDir + viewDir);
            float spec = pow(max(dot(vNormal, halfDir), 0.0), 64.0);

            vec3 baseColor;
            float stressLevel = clamp(vStress + uDamage * 0.5, 0.0, 1.0);

            if (stressLevel < 0.5) {
                vec3 coolBlue = vec3(0.45, 0.58, 0.82);
                vec3 midGray = vec3(0.55, 0.60, 0.68);
                baseColor = mix(coolBlue, midGray, stressLevel * 2.0);
            } else if (stressLevel < 0.75) {
                vec3 midGray = vec3(0.55, 0.60, 0.68);
                vec3 warningYellow = vec3(1.0, 0.76, 0.03);
                baseColor = mix(midGray, warningYellow, (stressLevel - 0.5) * 4.0);
            } else {
                vec3 warningYellow = vec3(1.0, 0.76, 0.03);
                vec3 dangerRed = vec3(1.0, 0.28, 0.34);
                baseColor = mix(warningYellow, dangerRed, (stressLevel - 0.75) * 4.0);
            }

            float ambient = 0.25;
            vec3 color = baseColor * (ambient + diff * 0.7) + vec3(1.0) * spec * 0.4;

            float damageCracks = 0.0;
            if (uDamage > 0.3) {
                float crackNoise = fract(sin(dot(vWorldPos.xz * 20.0, vec2(12.9898, 78.233))) * 43758.5453);
                damageCracks = smoothstep(0.98 - uDamage * 0.1, 1.0, crackNoise) * (uDamage - 0.3) * 2.0;
                color = mix(color, vec3(0.05, 0.03, 0.02), damageCracks);
            }

            float rim = pow(1.0 - max(dot(vNormal, viewDir), 0.0), 3.0) * 0.3;
            color += rim * baseColor;

            float glow = stressLevel * 0.4 + uDamage * 0.3;
            color += baseColor * glow * 0.25;

            gl_FragColor = vec4(color, 1.0);
        }
    `;

    function init(canvasId) {
        canvas = document.getElementById(canvasId);
        container = canvas.parentElement;

        const rect = container.getBoundingClientRect();

        scene = new THREE.Scene();
        scene.background = new THREE.Color(0x0a0e1a);

        camera = new THREE.PerspectiveCamera(45, rect.width / rect.height, 0.01, 100);
        camera.position.set(0.8, 0.2, 1.6);
        camera.lookAt(0, 0, 0);

        renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: false });
        renderer.setSize(rect.width, rect.height);
        renderer.setPixelRatio(window.devicePixelRatio);
        renderer.outputColorSpace = THREE.SRGBColorSpace;

        setupLights();
        createSpringMesh();
        createGroundGrid();
        setupControls();

        window.addEventListener('resize', onResize);
        animate();
    }

    function setupLights() {
        const ambient = new THREE.AmbientLight(0x404060, 0.5);
        scene.add(ambient);

        const keyLight = new THREE.DirectionalLight(0xfff0d4, 1.2);
        keyLight.position.set(3, 5, 4);
        scene.add(keyLight);

        const rimLight = new THREE.DirectionalLight(0x4488ff, 0.6);
        rimLight.position.set(-3, 2, -3);
        scene.add(rimLight);

        const fillLight = new THREE.PointLight(0xff8844, 0.5, 10);
        fillLight.position.set(0, -2, 1);
        scene.add(fillLight);
    }

    function createGroundGrid() {
        const gridGeo = new THREE.PlaneGeometry(6, 6, 30, 30);
        const gridMat = new THREE.MeshBasicMaterial({
            color: 0x1a2540,
            wireframe: true,
            transparent: true,
            opacity: 0.3
        });
        const grid = new THREE.Mesh(gridGeo, gridMat);
        grid.rotation.x = -Math.PI / 2;
        grid.position.y = -0.8;
        scene.add(grid);

        const planeGeo = new THREE.PlaneGeometry(6, 6);
        const planeMat = new THREE.MeshBasicMaterial({
            color: 0x0a1020,
            transparent: true,
            opacity: 0.6
        });
        const plane = new THREE.Mesh(planeGeo, planeMat);
        plane.rotation.x = -Math.PI / 2;
        plane.position.y = -0.801;
        scene.add(plane);
    }

    function createSpringMesh() {
        springGroup = new THREE.Group();
        scene.add(springGroup);

        const totalSegments = 512;
        const ringsPerSegment = 16;
        const totalVertices = totalSegments * ringsPerSegment;

        const positions = new Float32Array(totalVertices * 3);
        const indices = [];
        const segmentIndex = new Float32Array(totalVertices);
        const ringIndex = new Float32Array(totalVertices);

        let vi = 0;
        for (let i = 0; i < totalSegments; i++) {
            for (let j = 0; j < ringsPerSegment; j++) {
                positions[vi * 3] = 0;
                positions[vi * 3 + 1] = 0;
                positions[vi * 3 + 2] = 0;
                segmentIndex[vi] = i;
                ringIndex[vi] = j;
                vi++;
            }
        }

        for (let i = 0; i < totalSegments - 1; i++) {
            for (let j = 0; j < ringsPerSegment; j++) {
                const a = i * ringsPerSegment + j;
                const b = i * ringsPerSegment + ((j + 1) % ringsPerSegment);
                const c = (i + 1) * ringsPerSegment + j;
                const d = (i + 1) * ringsPerSegment + ((j + 1) % ringsPerSegment);
                indices.push(a, c, b);
                indices.push(b, c, d);
            }
        }

        const geometry = new THREE.BufferGeometry();
        geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
        geometry.setAttribute('aSegmentIndex', new THREE.BufferAttribute(segmentIndex, 1));
        geometry.setAttribute('aRingIndex', new THREE.BufferAttribute(ringIndex, 1));
        geometry.setIndex(indices);
        geometry.computeVertexNormals();

        const material = new THREE.ShaderMaterial({
            uniforms: {
                uTorsion: { value: 0.0 },
                uTurns: { value: springConfig.activeCoils },
                uSpringHeight: { value: 1.2 },
                uSpringRadius: { value: 0.45 },
                uWireRadius: { value: 0.05 },
                uDamage: { value: 0.0 },
                uTime: { value: 0.0 },
                uCycleCount: { value: 0.0 },
                uElapsed: { value: 10.0 }
            },
            vertexShader: springVertexShader,
            fragmentShader: springFragmentShader,
            side: THREE.DoubleSide
        });

        springMesh = new THREE.Mesh(geometry, material);
        springGroup.add(springMesh);

        createEndCaps();
    }

    function createEndCaps() {
        const capMat = new THREE.MeshStandardMaterial({
            color: 0x3a4a6a,
            metalness: 0.6,
            roughness: 0.4
        });

        const topCap = new THREE.Mesh(
            new THREE.CylinderGeometry(0.32, 0.32, 0.08, 24),
            capMat
        );
        topCap.position.y = 0.65;
        topCap.name = 'topCap';
        springGroup.add(topCap);

        const topPlate = new THREE.Mesh(
            new THREE.BoxGeometry(0.8, 0.06, 0.5),
            new THREE.MeshStandardMaterial({ color: 0x8b7355, roughness: 0.8 })
        );
        topPlate.position.y = 0.72;
        springGroup.add(topPlate);

        const bottomCap = new THREE.Mesh(
            new THREE.CylinderGeometry(0.32, 0.32, 0.08, 24),
            capMat
        );
        bottomCap.position.y = -0.65;
        bottomCap.name = 'bottomCap';
        springGroup.add(bottomCap);

        const pointerMat = new THREE.MeshStandardMaterial({
            color: 0xffb347,
            emissive: 0x553300,
            metalness: 0.8,
            roughness: 0.2
        });
        const pointer = new THREE.Mesh(
            new THREE.ConeGeometry(0.12, 0.4, 3),
            pointerMat
        );
        pointer.rotation.z = -Math.PI / 2;
        pointer.position.set(0.45, -0.72, 0);
        pointer.name = 'pointer';
        springGroup.add(pointer);
    }

    function setupControls() {
        let isDragging = false;
        let prev = { x: 0, y: 0 };
        let spherical = { theta: 0.8, phi: 1.1, radius: 2.0 };
        const target = new THREE.Vector3(0, 0, 0);

        canvas.addEventListener('mousedown', (e) => {
            isDragging = true;
            prev = { x: e.clientX, y: e.clientY };
        });
        canvas.addEventListener('mouseup', () => isDragging = false);
        canvas.addEventListener('mouseleave', () => isDragging = false);

        canvas.addEventListener('mousemove', (e) => {
            if (!isDragging) return;
            spherical.theta -= (e.clientX - prev.x) * 0.005;
            spherical.phi = Math.max(0.2, Math.min(Math.PI - 0.2,
                spherical.phi + (e.clientY - prev.y) * 0.005));
            prev = { x: e.clientX, y: e.clientY };
            updateCamera();
        });

        canvas.addEventListener('wheel', (e) => {
            e.preventDefault();
            spherical.radius = Math.max(0.8, Math.min(6, spherical.radius + e.deltaY * 0.003));
            updateCamera();
        });

        function updateCamera() {
            camera.position.x = target.x + spherical.radius * Math.sin(spherical.phi) * Math.cos(spherical.theta);
            camera.position.y = target.y + spherical.radius * Math.cos(spherical.phi);
            camera.position.z = target.z + spherical.radius * Math.sin(spherical.phi) * Math.sin(spherical.theta);
            camera.lookAt(target);
        }
        updateCamera();
    }

    function onResize() {
        if (!container) return;
        const rect = container.getBoundingClientRect();
        camera.aspect = rect.width / rect.height;
        camera.updateProjectionMatrix();
        renderer.setSize(rect.width, rect.height);
    }

    function setConfig(config) {
        springConfig = { ...springConfig, ...config };
        if (springMesh && springMesh.material.uniforms) {
            const u = springMesh.material.uniforms;
            u.uTurns.value = springConfig.activeCoils;
            u.uWireRadius.value = Math.max(0.01, springConfig.wireDiameter / 0.02 * 0.05);
            u.uSpringRadius.value = Math.max(0.15, springConfig.coilMeanDiameter / 0.15 * 0.45);
        }
    }

    function setTorsion(angleDeg) {
        targetTorsion = angleDeg;
    }

    function setCycleInfo(count, damage) {
        cycleCount = count;
        damageRatio = damage;
    }

    function animate() {
        animationId = requestAnimationFrame(animate);
        const t = performance.now() * 0.001;

        const diff = targetTorsion - currentTorsion;
        const absDiff = Math.abs(diff);
        const speed = absDiff > 10 ? 0.25 : 0.1;
        currentTorsion += diff * speed;

        if (springMesh) {
            const u = springMesh.material.uniforms;
            u.uTorsion.value = TrebuchetPhysics.degToRad(currentTorsion);
            u.uTime.value = t;
            u.uDamage.value = damageRatio;
            u.uCycleCount.value = cycleCount;

            if (absDiff > 0.5 && u.uElapsed.value > 0.8) {
                u.uElapsed.value = 0;
            }
            if (u.uElapsed.value < 2.0) {
                u.uElapsed.value += 0.016;
            }
        }

        const bottomCap = springGroup.getObjectByName('bottomCap');
        const pointer = springGroup.getObjectByName('pointer');
        if (bottomCap) {
            bottomCap.rotation.y = TrebuchetPhysics.degToRad(currentTorsion) * 0.3;
        }
        if (pointer) {
            pointer.parent.updateMatrixWorld();
            pointer.position.x = 0.45 + Math.abs(TrebuchetPhysics.degToRad(currentTorsion)) * 0.3;
            pointer.rotation.y = TrebuchetPhysics.degToRad(currentTorsion) * 0.2;
        }

        renderer.render(scene, camera);
    }

    function getCurrentEnergy() {
        const torsionRad = TrebuchetPhysics.degToRad(currentTorsion);
        return TrebuchetPhysics.calculateSpringEnergy(springConfig, torsionRad);
    }

    function getConfig() {
        return springConfig;
    }

    function stop() {
        if (animationId) cancelAnimationFrame(animationId);
    }

    return {
        init,
        setConfig,
        setTorsion,
        setCycleInfo,
        getCurrentEnergy,
        getConfig,
        stop
    };
})();
